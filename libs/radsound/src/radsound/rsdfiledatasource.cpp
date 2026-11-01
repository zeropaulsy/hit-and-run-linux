//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include "pch.hpp"
#include <radsoundfile.hpp>
#include "rsdfiledatasource.hpp"
#include "radicaladpcm.hpp"
#ifdef RAD_LINUX
#include "linux/oggdecoder.hpp"
#endif

RadSoundFilePerformanceCallback * gpFpc;

void Log(
    bool start,
    const char * pFile,
    unsigned int bytes )
{
    if ( gpFpc )
    {
        gpFpc( start, pFile, bytes );
    }
}    
    
void RadSoundSetFilePerformanceCallback( RadSoundFilePerformanceCallback * pCallback )
{
    gpFpc = pCallback;
}


radSoundRsdFileDataSource::radSoundRsdFileDataSource( void )
    :
    m_Decode( false ),
    m_Channels( 0 ),
    m_AdpcmBlockBytes( 0 ),
    m_pAdpcm( NULL ),
    m_AdpcmCapacity( 0 ),
    m_CarryOffset( 0 ),
    m_CarryFrames( 0 ),
    m_pDecodeDest( NULL ),
    m_DecodeFrames( 0 ),
    m_pOgg( NULL ),
    m_pOggData( NULL ),
    m_OggInitialPos( 0 ),
    m_OggInitialPosUnits( IRadSoundHalAudioFormat::Frames )
{
	m_StateInfo.m_State = StateInfo::NONE;
}

radSoundRsdFileDataSource::~radSoundRsdFileDataSource( void )
{
    if ( m_pAdpcm != NULL )
    {
        ::radMemoryFreeAligned( GetThisAllocator( ), m_pAdpcm );
    }
#ifdef RAD_LINUX
    radOggDecoderClose( m_pOgg );
    free( m_pOggData );
#endif
}

IRadSoundHalDataSource::State radSoundRsdFileDataSource::GetState( void )
{
    if ( m_StateInfo.m_State == StateInfo::INITIALIZED )
    {
        if ( m_refIRadFile == NULL )
        {            
	        ::radFileOpen(
	            & m_refIRadFile,
	            fileName,
	            false,
	            OpenExisting,
                NormalPriority, // m_StateInfo.m_InitializedInfo.m_HighPriority ? HighPriority : NormalPriority,
                0,
                RADMEMORY_ALLOC_TEMP,
                radMemorySpace_Local );
        }
        	    
	    m_StateInfo.m_State = StateInfo::OPENING_FILE;
	     
        m_refIRadFile->AddCompletionCallback( this, NULL );
 
    }
    else if (
        m_StateInfo.m_State == StateInfo::IDLE ||
	    m_StateInfo.m_State == StateInfo::READING_DATA )
	{
		return IRadSoundHalDataSource::Initialized;
	}
	else if ( m_StateInfo.m_State == StateInfo::FILE_ERROR )
	{
		return IRadSoundHalDataSource::Error;
	}
	
	return IRadSoundHalDataSource::Initializing;
}

void radSoundRsdFileDataSource::InitializeFromFile
( 
	IRadFile * pIRadFile,
	unsigned int initialPlaybackPosition,
	IRadSoundHalAudioFormat::SizeType positionType,
    IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat     
)
{
	rAssert( m_StateInfo.m_State == StateInfo::NONE );
	rAssert( pIRadFile != NULL );

    m_refIRadSoundHalAudioFormat = pIRadSoundHalAudioFormat;

    strcpy( fileName, "" );
    
	// We can't convert these values until we an audio format

    m_StateInfo.m_State = StateInfo::INITIALIZED;
    m_StateInfo.m_InitializedInfo.m_InitialPlaybackPos = initialPlaybackPosition;
    m_StateInfo.m_InitializedInfo.m_InitalPlaybackPosUnits = positionType;    
    m_StateInfo.m_InitializedInfo.m_HighPriority = true;
	//
	// We have a file that may or may not be open
	// We'll get the file system to call us back 
	// when the file is open and this will start our
	// state machine
	//

	m_refIRadFile = pIRadFile;

}

void radSoundRsdFileDataSource::InitializeFromFileName
( 
	const char * pFileName,
    bool highPriority,
	unsigned int initialPlaybackPosition,
	IRadSoundHalAudioFormat::SizeType positionType,
    IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat
)
{
    rAssert( pFileName != NULL );
	rAssert( m_StateInfo.m_State == StateInfo::NONE );

    m_refIRadSoundHalAudioFormat = pIRadSoundHalAudioFormat;
    
    strncpy( fileName, pFileName, 64 );
			
	m_StateInfo.m_State = StateInfo::INITIALIZED;
	m_StateInfo.m_InitializedInfo.m_InitalPlaybackPosUnits = positionType;
	m_StateInfo.m_InitializedInfo.m_InitialPlaybackPos = initialPlaybackPosition;
	m_StateInfo.m_InitializedInfo.m_HighPriority = highPriority;
};

IRadSoundHalAudioFormat * radSoundRsdFileDataSource::GetFormat( void )
{
    rAssert( m_StateInfo.m_State == StateInfo::IDLE || m_StateInfo.m_State == StateInfo::READING_DATA );
    rAssert( m_refIRadSoundHalAudioFormat != NULL );

	return m_refIRadSoundHalAudioFormat;
}

unsigned int radSoundRsdFileDataSource::GetRemainingFrames( void )
{
    rAssert( m_StateInfo.m_State == StateInfo::IDLE || m_StateInfo.m_State == StateInfo::READING_DATA );
	return m_FramesLeftInFile;
}

void radSoundRsdFileDataSource::GetFramesAsync
(
	void * pFrameBuffer,
	radMemorySpace destinationMemorySpace,
	unsigned int sizeInFrames,
	IRadSoundHalDataSourceCallback * pIRshdsc
)
{
    rAssert( m_StateInfo.m_State == StateInfo::IDLE );
	rAssert( pIRshdsc != NULL );

    /* rDebugPrintf( "FDS: [%s]\n\tthis: [0x%x] Request for [0x%x] frames from: [0x%x] @ [0x%x]\n",
        this->fileName,
        this,
        sizeInFrames,
        pIRshdsc,
        pFrameBuffer ); */
        
	if ( m_FramesLeftInFile < sizeInFrames )
	{
		sizeInFrames = m_FramesLeftInFile;
	}

	m_StateInfo.m_State = StateInfo::READING_DATA;

	// Get a read going

	m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames = sizeInFrames;
	m_refIRadSoundHalDataSourceCallback = pIRshdsc;

#ifdef RAD_LINUX
    if ( m_pOgg != NULL )
    {
        // Decode now; a stream shorter than its granule position said ends in silence
        unsigned int got = radOggDecoderRead( m_pOgg, (short*) pFrameBuffer, sizeInFrames );
        unsigned int frameBytes = radOggDecoderChannels( m_pOgg ) * 2;
        memset( (char*) pFrameBuffer + got * frameBytes, 0, ( sizeInFrames - got ) * frameBytes );

        // Called back once the file system next completes, so never re-entrantly (as for RADP above)
        m_refIRadFile->AddCompletionCallback( this, NULL );
        return;
    }
#endif

    if ( m_Decode )
    {
        // Serve what is left of the last block first, then read whole blocks for the rest

        unsigned int frameBytes = m_Channels * 2;
        unsigned int fromCarry = ( m_CarryFrames < sizeInFrames ) ? m_CarryFrames : sizeInFrames;
        memcpy( pFrameBuffer, m_Carry + m_CarryOffset * m_Channels, fromCarry * frameBytes );
        m_CarryOffset += fromCarry;
        m_CarryFrames -= fromCarry;

        m_pDecodeDest = (char*) pFrameBuffer + fromCarry * frameBytes;
        m_DecodeFrames = sizeInFrames - fromCarry;

        unsigned int blocks = ( m_DecodeFrames + RADICAL_ADPCM_SAMPLES_PER_FRAME - 1 ) / RADICAL_ADPCM_SAMPLES_PER_FRAME;
        unsigned int bytes = blocks * m_AdpcmBlockBytes;

        if ( bytes > 0 )
        {
            if ( bytes > m_AdpcmCapacity )
            {
                if ( m_pAdpcm != NULL )
                {
                    ::radMemoryFreeAligned( GetThisAllocator( ), m_pAdpcm );
                }
                m_AdpcmCapacity = ::radMemoryRoundUp( bytes, 4096 );
                m_pAdpcm = ::radMemoryAllocAligned( GetThisAllocator( ), m_AdpcmCapacity, radFileOptimalMemoryAlignment );
            }

            Log( true, this->fileName, bytes );
            m_refIRadFile->ReadAsync( m_pAdpcm, bytes );
        }

        // Called back once the read (if any) is done, so the client is never called back re-entrantly
        m_refIRadFile->AddCompletionCallback( this, NULL );
        return;
    }

	// Figure out how many bytes we'll require
	// (And verify that our conversion is happening correctly)

	unsigned int bytes = m_refIRadSoundHalAudioFormat->FramesToBytes( sizeInFrames );
    
    /* if ( ((unsigned int)pFrameBuffer % radSoundStreamReadAlignmentGet( ) ) != 0 )
    {
        rDebugPrintf( "radSoundRsdFileDataSource: reading non-optimal alignment!\n" );
    } */

    // unsigned int optimalFrameReadMultiple = m_refIRadSoundHalAudioFormat->BytesToFrames( radSoundStreamReadMultipleGet( ) );

    /* if ( m_FramesLeftInFile > sizeInFrames )
    {
        if ( (sizeInFrames % optimalFrameReadMultiple ) != 0 )
        {
            rDebugPrintf( "radSoundRsdFileDataSource:: reading non-optimal size!\n" );
        }
    } */
    
    Log( true, this->fileName, bytes );
    
#ifdef RAD_PS2    
    //bytes = radMemoryRoundUp( bytes, 2048 );
#endif
    
	m_refIRadFile->ReadAsync(pFrameBuffer, bytes, destinationMemorySpace );
	m_refIRadFile->AddCompletionCallback( this, NULL );

}

void radSoundRsdFileDataSource::OnFileOperationsComplete( void* pUserData )
{
	StateInfo::State state = m_StateInfo.m_State;

	switch ( m_StateInfo.m_State )
	{
		case StateInfo::NONE:
		{
			// do nothing
			break;
		}
		case StateInfo::OPENING_FILE:
		{
			_StateOpeningFile( );
			break;
		}
		case StateInfo::READING_HEADER:
		{
            Log( false, this->fileName, 0 );		
			_StateReadingHeader( );
			break;
		}
		case StateInfo::IDLE:
		{
			_StateIdle( );
			break;
		}
		case StateInfo::READING_DATA:
		{
            Log( false, this->fileName, 0 );		
			_StateReadingData( );
			break;
		}
#ifdef RAD_LINUX
		case StateInfo::READING_OGG:
		{
			_StateReadingOgg( );
			break;
		}
#endif
        default:
        {
            rAssert( false );
        }
	};
}

void radSoundRsdFileDataSource::_StateOpeningFile( void )
{
	if( m_refIRadFile->IsOpen( ) == true )
	{
        m_refIRadFile->SetBufferedRead( IRadFile::BufferedReadOn );
        // Linux port: always read the header, even with a client format, to find RADP files
        {
            if ( m_refIRadSoundHalAudioFormat == NULL )
            {
                rDebugPrintf(
                    "radSoundRsdFileDataSource: WARNING reading header for: [%s], extra seek incurred\n",
                    fileName );
            }
               
		    //
		    // The file was opened correctly.  We can read in the
		    // header and find out what kind of a file this is.
		    //

            unsigned int initialPlaybackPos = m_StateInfo.m_InitializedInfo.m_InitialPlaybackPos;
            IRadSoundHalAudioFormat::SizeType initialPlaybackPosUnits
                = m_StateInfo.m_InitializedInfo.m_InitalPlaybackPosUnits;
            
		    m_StateInfo.m_State = StateInfo::READING_HEADER;
            m_StateInfo.m_ReadingHeaderInfo.m_InitialPlaybackPos = initialPlaybackPos;
            m_StateInfo.m_ReadingHeaderInfo.m_InitalPlaybackPosUnits = initialPlaybackPosUnits;
            
		    // Prepare a temporary structure to hold the header info

		    m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader =
			    static_cast< radSoundHalFileHeader * >(
				    ::radMemoryAllocAligned(
					    RADMEMORY_ALLOC_TEMP,
					    ::radMemoryRoundUp( sizeof( radSoundHalFileHeader ), radFileMaxSectorSize ),
					    radFileOptimalMemoryAlignment ) );

            ::radMemoryMonitorIdentifyAllocation(
                m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader,
                radSoundDebugChannel,
                "radSoundRsdFileDataSource::m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader" );                    

		    // Set the read position to the start of the file and
		    // read in the header
		    

            Log( true, this->fileName, sizeof( radSoundHalFileHeader ) );
    		    
		    m_refIRadFile->SetPositionAsync( 0 );
		    m_refIRadFile->ReadAsync(
			    m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader,
			    sizeof( radSoundHalFileHeader ) );
		    
		    m_refIRadFile->AddCompletionCallback( this, NULL );
        }
	}
	else
	{
		rTunePrintf( "Failed to open file: [%s]\n", m_refIRadFile->GetFilename( ) );

		//
		// The file system didn't find our file.  Say that the
		// size of the file is zero and move into the idle state.
		// We don't need to fail because of this problem.
		//

		m_StateInfo.m_State = StateInfo::FILE_ERROR;
		m_FramesLeftInFile = 0;
		m_refIRadFile = NULL;
	}
}

void radSoundRsdFileDataSource::_StateReadingHeader( void )
{
	rAssert(  m_refIRadFile->IsOpen( ) );  // If the file isn't open, we shouldn't be here

#ifdef RAD_LINUX
    if ( radOggIsOgg( m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader ) ||
         radOggIsFlac( m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader ) )
    {
        // An Ogg or FLAC file under an .rsd name: read all of it, then decode as it plays
        ::radMemoryFreeAligned( RADMEMORY_ALLOC_TEMP, m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader );
        m_OggInitialPos = m_StateInfo.m_ReadingHeaderInfo.m_InitialPlaybackPos;
        m_OggInitialPosUnits = m_StateInfo.m_ReadingHeaderInfo.m_InitalPlaybackPosUnits;
        unsigned int size = m_refIRadFile->GetSize( );
        m_pOggData = (unsigned char*) malloc( size > 0 ? size : 1 );
        m_StateInfo.m_State = StateInfo::READING_OGG;
        m_refIRadFile->SetPositionAsync( 0 );
        m_refIRadFile->ReadAsync( m_pOggData, size );
        m_refIRadFile->AddCompletionCallback( this, NULL );
        return;
    }
#endif

	// Get the data under control for strange platforms

	m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader->ConvertToPlatformEndian( );

	// Create a new audio format object

	ref< IRadSoundHalAudioFormat > refFileFormat = ::radSoundHalAudioFormatCreate( GetThisAllocator( ) );

	m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader->InitializeAudioFormat(
		refFileFormat, GetThisAllocator( ) );

#ifdef RAD_LINUX
    if ( getenv( "SHAR_RSD_LOG" ) != NULL )
    {
        fprintf( stderr, "rsd: %s enc %d rate %d ch %d client %d\n", fileName, (int) refFileFormat->GetEncoding( ),
            (int) refFileFormat->GetSampleRate( ), (int) refFileFormat->GetNumberOfChannels( ),
            m_refIRadSoundHalAudioFormat == NULL ? -1 : (int) m_refIRadSoundHalAudioFormat->GetEncoding( ) );
    }
#endif
    if ( m_refIRadSoundHalAudioFormat == NULL )
    {
        m_refIRadSoundHalAudioFormat = refFileFormat;
    }
    else if ( refFileFormat->GetEncoding( ) == IRadSoundHalAudioFormat::RadicalAdpcm &&
              m_refIRadSoundHalAudioFormat->GetEncoding( ) == IRadSoundHalAudioFormat::PCM &&
              refFileFormat->GetNumberOfChannels( ) <= 2 )
    {
        // Decode to PCM with the file's own rate and channel count
        m_Decode = true;
        m_Channels = refFileFormat->GetNumberOfChannels( );
        m_AdpcmBlockBytes = RADICAL_ADPCM_FRAME_SIZE * m_Channels;
        m_refIRadSoundHalAudioFormat = ::radSoundHalAudioFormatCreate( GetThisAllocator( ) );
        m_refIRadSoundHalAudioFormat->Initialize( IRadSoundHalAudioFormat::PCM, NULL,
            refFileFormat->GetSampleRate( ), m_Channels, 16 );
    }
	
	// Release the tempory memory

	::radMemoryFreeAligned( RADMEMORY_ALLOC_TEMP, m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader );
	m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader = NULL;

    InitFile( );
}

void radSoundRsdFileDataSource::InitFile( void )
{
    if ( m_Decode )
    {
        // Positions are in PCM frames; start on the block holding the requested position

        unsigned int fileBlocks = ( m_refIRadFile->GetSize( ) - RSD_FILE_DATA_OFFSET ) / m_AdpcmBlockBytes;
        rAssertMsg( ( m_refIRadFile->GetSize( ) - RSD_FILE_DATA_OFFSET ) % m_AdpcmBlockBytes == 0,
            "File Data is not block aligned (corrupted )" );

        unsigned int startFrame = m_refIRadSoundHalAudioFormat->ConvertSizeType(
            IRadSoundHalAudioFormat::Frames,
            m_StateInfo.m_ReadingHeaderInfo.m_InitialPlaybackPos,
            m_StateInfo.m_ReadingHeaderInfo.m_InitalPlaybackPosUnits );
        unsigned int startBlock = startFrame / RADICAL_ADPCM_SAMPLES_PER_FRAME;
        rAssert( startBlock < fileBlocks );
        if ( startBlock > fileBlocks )
        {
            startBlock = fileBlocks;
        }

        m_FramesLeftInFile = ( fileBlocks - startBlock ) * RADICAL_ADPCM_SAMPLES_PER_FRAME;
        m_CarryOffset = 0;
        m_CarryFrames = 0;

        m_StateInfo.m_State = StateInfo::IDLE;
        m_refIRadFile->SetPositionAsync( RSD_FILE_DATA_OFFSET + startBlock * m_AdpcmBlockBytes );
        ::radSoundHalSystemGet( )->Service( );
        return;
    }

	// Figure out where to start reading in the file

	unsigned int initialPlaybackOffsetInBytes =
	    m_refIRadSoundHalAudioFormat->ConvertSizeType(
		    IRadSoundHalAudioFormat::Bytes,
		    m_StateInfo.m_ReadingHeaderInfo.m_InitialPlaybackPos,
		    m_StateInfo.m_ReadingHeaderInfo.m_InitalPlaybackPosUnits );

	// Ensure that the offset is a multiple of the framesize

	initialPlaybackOffsetInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes(
		m_refIRadSoundHalAudioFormat->BytesToFrames( initialPlaybackOffsetInBytes ) );

	// Calculate the amount of data contained in the file

	m_FramesLeftInFile = m_refIRadSoundHalAudioFormat->BytesToFrames(
		m_refIRadFile->GetSize( ) - RSD_FILE_DATA_OFFSET );

	rAssert( m_refIRadSoundHalAudioFormat->BytesToFrames( initialPlaybackOffsetInBytes ) < m_FramesLeftInFile );
	m_FramesLeftInFile -= m_refIRadSoundHalAudioFormat->BytesToFrames( initialPlaybackOffsetInBytes );

	//
	// An important assertion: The filesize needs to be a multiple of the framesize
	//
	unsigned int fileDataSize = m_refIRadFile->GetSize( ) - RSD_FILE_DATA_OFFSET - initialPlaybackOffsetInBytes;
	unsigned int roundedBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( m_FramesLeftInFile );
	rAssertMsg( fileDataSize == roundedBytes, "File Data is not block aligned (corrupted )" );

	// Move into the idle state before possibly getting called back

	m_StateInfo.m_State = StateInfo::IDLE;

	// Skip to the data and include the clients requested start position

	m_refIRadFile->SetPositionAsync( RSD_FILE_DATA_OFFSET + initialPlaybackOffsetInBytes );

    // Bit of a hack here, service the sound system so streamers pool the IsInitialized()
    // flag right now.

    ::radSoundHalSystemGet( )->Service( );
}

void radSoundRsdFileDataSource::_StateIdle( void )
{
}

#ifdef RAD_LINUX
void radSoundRsdFileDataSource::_StateReadingOgg( void )
{
    const char* name = m_refIRadFile->GetFilename( );
    m_pOgg = radOggDecoderOpen( m_pOggData, m_refIRadFile->GetSize( ), name );
    m_pOggData = NULL;                                  // the decoder owns it now
    if ( m_pOgg == NULL )
    {
        m_StateInfo.m_State = StateInfo::FILE_ERROR;
        m_FramesLeftInFile = 0;
        m_refIRadFile = NULL;
        return;
    }

    unsigned int channels = radOggDecoderChannels( m_pOgg );
    m_refIRadSoundHalAudioFormat = ::radSoundHalAudioFormatCreate( GetThisAllocator( ) );
    m_refIRadSoundHalAudioFormat->Initialize( IRadSoundHalAudioFormat::PCM, NULL,
        radOggDecoderRate( m_pOgg ), channels, 16 );

    unsigned int total = radOggDecoderTotalFrames( m_pOgg );
    unsigned int start = m_refIRadSoundHalAudioFormat->ConvertSizeType(
        IRadSoundHalAudioFormat::Frames, m_OggInitialPos, m_OggInitialPosUnits );
    if ( start > total )
    {
        start = total;
    }
    radOggDecoderRead( m_pOgg, NULL, start );
    m_FramesLeftInFile = total - start;
    if ( getenv( "SHAR_RSD_LOG" ) != NULL )
    {
        fprintf( stderr, "rsd: %s decoded: rate %u ch %u frames %u from %u\n", name, radOggDecoderRate( m_pOgg ),
            channels, total, start );
    }

    m_StateInfo.m_State = StateInfo::IDLE;
    ::radSoundHalSystemGet( )->Service( );
}
#endif

void radSoundRsdFileDataSource::DecodeReadBlocks( void )
{
    // Whole blocks straight into the client's buffer, a final partial block via m_Carry

    unsigned int frameBytes = m_Channels * 2;
    unsigned int fullBlocks = m_DecodeFrames / RADICAL_ADPCM_SAMPLES_PER_FRAME;
    unsigned int partial = m_DecodeFrames % RADICAL_ADPCM_SAMPLES_PER_FRAME;

    if ( fullBlocks > 0 )
    {
        DecodeFrames( m_pDecodeDest, m_pAdpcm, fullBlocks, m_Channels );
    }

    if ( partial > 0 )
    {
        DecodeFrames( m_Carry, (char*) m_pAdpcm + fullBlocks * m_AdpcmBlockBytes, 1, m_Channels );
        memcpy( m_pDecodeDest + fullBlocks * RADICAL_ADPCM_SAMPLES_PER_FRAME * frameBytes, m_Carry, partial * frameBytes );
        m_CarryOffset = partial;
        m_CarryFrames = RADICAL_ADPCM_SAMPLES_PER_FRAME - partial;
    }

#ifdef RAD_LINUX
    if ( getenv( "SHAR_RSD_LOG" ) != NULL )
    {
        int peak = 0;
        const short* s = (const short*) m_pDecodeDest;
        for ( unsigned int i = 0; i < m_DecodeFrames * m_Channels; i++ )
        {
            int v = s[ i ] < 0 ? -s[ i ] : s[ i ];
            if ( v > peak ) peak = v;
        }
        fprintf( stderr, "rsd: %s decoded %u frames, peak %d\n", fileName, m_DecodeFrames, peak );
    }
#endif
    m_DecodeFrames = 0;
}

void radSoundRsdFileDataSource::_StateReadingData( void )
{
    if ( m_Decode )
    {
        DecodeReadBlocks( );
    }

	// Update our position info and callback the client

	m_FramesLeftInFile -= m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames;
	m_StateInfo.m_State = StateInfo::IDLE;

	ref< IRadSoundHalDataSourceCallback > xIRadSoundHalDataSourceCallback = m_refIRadSoundHalDataSourceCallback;
	m_refIRadSoundHalDataSourceCallback = NULL;

    /* rDebugPrintf( "FDS:[%s]\n\tthis:[0x%x] Calling back for [0x%x] frames callback: [0x%x]\n",
        this->fileName,
        this,
        m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames,
        xIRadSoundHalDataSourceCallback.m_pInterface ); */
        
	xIRadSoundHalDataSourceCallback->OnDataSourceFramesLoaded( m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames );
}

IRadSoundRsdFileDataSource * radSoundRsdFileDataSourceCreate( radMemoryAllocator allocator )
{
	return new ( "radSoundRsdFileDataSource", allocator ) radSoundRsdFileDataSource( );
}
