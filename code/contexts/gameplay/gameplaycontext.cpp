//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        
//
// Description: Implement GameplayContext
//
// History:     21/05/2002 + Created -- NAME
//
//=============================================================================

//========================================
// System Includes
//========================================
// Foundation Tech
#include <raddebug.hpp>
#include <radtime.hpp>
#include <raddebugwatch.hpp>

//========================================
// Project Includes
//========================================

#include <ai/vehicle/vehicleairender.h>

#include <camera/animatedcam.h>
#include <camera/debugcam.h>
#include <camera/followcam.h> 
#include <camera/relativeanimatedcam.h>
#include <camera/chasecam.h>
#include <camera/bumpercam.h>
#include <camera/kullcam.h>
#include <camera/trackercam.h>
#include <camera/walkercam.h>
#include <camera/railcam.h>
#include <camera/wrecklesscam.h>
#include <camera/supercammanager.h>
#include <camera/conversationcam.h>
#include <camera/reversecam.h>
#include <camera/snapshotcam.h>
#ifdef RAD_WIN32
#include <camera/pccam.h>
#endif

//#include <camera/firstpersoncam.h>

#include <contexts/gameplay/gameplaycontext.h>
#include <contexts/contextenum.h>
#include <events/eventmanager.h>
#include <gameflow/gameflow.h>

#include <data/gamedatamanager.h>

#include <interiors/interiormanager.h>
#include <memory/leakdetection.h>
#include <memory/srrmemory.h>

#include <meta/triggervolumetracker.h>

#include <mission/gameplaymanager.h>
#include <mission/charactersheet/charactersheetmanager.h>

#include <presentation/presentation.h>
#include <presentation/gui/guisystem.h>

#include <render/RenderManager/RenderManager.h>
#include <render/RenderManager/RenderLayer.h>

#include <worldsim/character/footprint/footprintmanager.h>
#include <worldsim/worldphysicsmanager.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/avatar.h>
#include <loading/loadingmanager.h>
#include <main/commandlineoptions.h>
#include <worldsim/coins/coinmanager.h>
#include <worldsim/skidmarks/skidmarkmanager.h>
#include <worldsim/coins/sparkle.h>
#include <worldsim/hitnrunmanager.h>
#include <ai/actionbuttonmanager.h>
#include <ai/actor/actormanager.h>


#include <worldsim/character/charactermanager.h>
#include <worldsim/character/character.h>
#include <worldsim/traffic/trafficmanager.h>
#include <worldsim/ped/pedestrianmanager.h>

#include <render/breakables/breakablesmanager.h>
#include <render/Particles/particlemanager.h>
#include <render/animentitydsgmanager/animentitydsgmanager.h>
#include <render/RenderManager/WorldRenderLayer.h>
#include <worldsim/skidmarks/SkidMarkGenerator.h>

#include <sound/soundmanager.h>

#include <roads/roadmanager.h>

#include <input/inputmanager.h>
#include <p3d/light.hpp>
#include <p3d/view.hpp>
#include <shar_mods.h>

//******************************************************************************
//
// Global Data, Local Data, Local Classes
//
//******************************************************************************

// Static pointer to instance of singleton.
GameplayContext* GameplayContext::spInstance = NULL;

//******************************************************************************
//
// Public Member Functions
//
//******************************************************************************

//==============================================================================
// GameplayContext::GetInstance
//==============================================================================
//
// Description: - Access point for the GameplayContext singleton.  
//              - Creates the GameplayContext if needed.
//
// Parameters:	None.
//
// Return:      Pointer to the GameplayContext.
//
// Constraints: This is a singleton so only one instance is allowed.
//
//==============================================================================
GameplayContext* GameplayContext::GetInstance()
{
    if( spInstance == NULL )
    {
        spInstance = new(GMA_PERSISTENT) GameplayContext;
        rAssert( spInstance );
    }
    
    return spInstance;
}

//==============================================================================
// GameplayContext::GameplayContext
//==============================================================================
// Description: Constructor.
//
// Parameters: None.
//
// Return:      N/A.
//
//==============================================================================
GameplayContext::GameplayContext() :
    m_PausedAllButPresentation( false ),
    mSlowMoHack( false )
{
#ifdef DEBUGWATCH
    radDbgWatchAddUnsignedInt( &mDebugPhysTiming, "Debug Phys micros", "GameplayContext", NULL, NULL );
    radDbgWatchAddUnsignedInt( &mDebugTimeDelta, "Debug dT micros", "GameplayContext", NULL, NULL );
    radDbgWatchAddUnsignedInt( &mDebugOnUpdateDT, "Gameplay dT micros", "GameplayContext", NULL, NULL );
    radDbgWatchAddBoolean(&mSlowMoHack, "Slow Mo Hack", "GameplayContext", NULL, NULL);
#endif
}

//==============================================================================
// GameplayContext::~GameplayContext
//==============================================================================
// Description: Destructor.
//
// Parameters: None.
//
// Return:      N/A.
//
//==============================================================================
GameplayContext::~GameplayContext()
{
#ifdef DEBUGWATCH
    radDbgWatchDelete(&mDebugPhysTiming);
    radDbgWatchDelete(&mDebugTimeDelta);
    radDbgWatchDelete(&mDebugOnUpdateDT);
    
    radDbgWatchDelete(&mSlowMoHack);
#endif
}

//******************************************************************************
//
// Private Member Functions
//
//******************************************************************************

//=============================================================================
// GameplayContext::OnStart
//=============================================================================
// Description: Comment
//
// Parameters:  ( ContextEnum previousContext )
//
// Return:      void 
//
//=============================================================================
void GameplayContext::OnStart( ContextEnum previousContext )
{
    // Common to all playing contexts.
    //
    PlayingContext::OnStart( previousContext );

    MEMTRACK_PUSH_FLAG( "Gameplay" );
    // do gameplay initializations only if previous context was loading context
    if( previousContext == CONTEXT_LOADING_GAMEPLAY )
    {
        // RenderManager
        //
        RenderManager* rm = GetRenderManager();

        RenderLayer* rl = rm->mpLayer( RenderEnums::LevelSlot );
        rAssert( rl );

#ifdef DEBUGWATCH
        // bootstrap vehicleai renderer
        VehicleAIRender::GetVehicleAIRender();
#endif

        // Set up cameras
        //
        unsigned int iNumPlayers = GetGameplayManager()->GetNumPlayers();

        rl->SetNumViews( iNumPlayers );
        rl->SetUpViewCam();

		p3d::inventory->SelectSection("Default");
        tLightGroup* sun = p3d::find<tLightGroup>("sun");   
        rAssert( sun );
        rm->SetLevelLayerLights( sun );

        float aspect = p3d::display->IsWidescreen() ? (16.0f / 9.0f) : (4.0f / 3.0f);

        if( iNumPlayers == 2 )
        {
            aspect = 4.0f / 1.5f;
        }

        for( unsigned int view = 0; view < iNumPlayers; view++ )
        {

            tPointCamera* cam = static_cast<tPointCamera*>( rl->pCam( view ) );
            rAssert( dynamic_cast<tPointCamera*> ( cam ) != NULL );
            rAssert( cam );

            SuperCamCentral* scc = GetSuperCamManager()->GetSCC( view );
            rAssert( scc );
            scc->SetCamera( cam );
    
            FollowCam* fc = new FollowCam( FollowCam::FOLLOW_NEAR );
            fc->SetAspect( aspect );
            fc->CopyToData();
            scc->RegisterSuperCam( fc );

            fc = new FollowCam( FollowCam::FOLLOW_FAR );
            fc->SetAspect( aspect );
            fc->CopyToData();
            scc->RegisterSuperCam( fc );

            SuperCam* sc = new BumperCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new ChaseCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new KullCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new DebugCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new TrackerCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new WalkerCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new ComedyCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new WrecklessCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new ConversationCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new ReverseCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new AnimatedCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
            sc = new RelativeAnimatedCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
//            sc = new FirstPersonCam();
//            sc->SetAspect( aspect );
//            scc->RegisterSuperCam( sc );
#ifdef RAD_WIN32
            sc = new PCCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
#endif
#if !defined(FINAL) && defined(RAD_XBOX)
            sc = new SnapshotCam();
            sc->SetAspect( aspect );
            scc->RegisterSuperCam( sc );
#endif
        }

        // Boot strap the AvatarManager.
        //
        GetAvatarManager( )->EnterGame( );
        GetActionButtonManager( )->EnterGame( );

        TrafficManager::GetInstance()->Init();
        PedestrianManager::GetInstance()->Init();
        
        // Prepare the manager to start gameplay
        //
        //GetGameplayManager()->Reset();

#ifdef RAD_DEMO
        GetGameplayManager()->ResetIdleTime();
#endif

        // Start up GUI in-game manager
        GetGuiSystem()->HandleMessage( GUI_MSG_RUN_INGAME );

        // register GUI user input handler for active players only
        //
        int activeControllerIDs = 0;
        for( int i = 0; i < MAX_PLAYERS; i++ )
        {
            int controllerID = GetInputManager()->GetControllerIDforPlayer( i );
            if( controllerID != -1 )
            {
                activeControllerIDs |= (1 << controllerID);
            }
        }

        GetGuiSystem()->RegisterUserInputHandlers( activeControllerIDs );

        // Tell GameData Manager that game has been started
        //
        GetGameDataManager()->SetGameLoaded();

        //Set up the sorting of the intersections and stuff.
        RoadManager::GetInstance()->CreateRoadNetwork();

        //Find skid mark shaders in the inventory and set proper values
        SkidMarkGenerator::InitShaders();

        GetSkidmarkManager()->Init( shar_ModLimit( "Cars", "SkidMarkLimit", DEFAULT_NUM_SKID_MARKS ) ); // Linux port: CustomLimits

        //
        // Notify the sound system of gameplay start.  This has been moved after
        // the GUI startup above, since that leads to a mission reset, which causes
        // character position in or out of the car to be decided, which the sound
        // system uses to determine which sounds to start playing.
        //
        SoundManager::GetInstance()->OnGameplayStart();

        GetInteriorManager()->OnGameplayStart();

        GetHitnRunManager()->ResetState();

        //tick character manager once, to get state system and choreo started
        GetCharacterManager()->PreSimUpdate( 0.0001f );
        GetCharacterManager()->PreSubstepUpdate( 0.0001f );
        GetCharacterManager()->Update( 0.0001f );
        GetCharacterManager()->PostSubstepUpdate( 0.0001f );
        GetCharacterManager()->PostSimUpdate( 0.0001f );

    }

    GetPresentationManager()->OnGameplayStart();

    HeapMgr()->ResetArtStats();
    GetEventManager()->TriggerEvent( EVENT_LEVEL_START );

    if( previousContext != CONTEXT_PAUSE )
    {
        AnimatedCam::CheckPendingCameraSwitch();
    }

    for( int view = 0; view < GetGameplayManager()->GetNumPlayers(); view++ )
    {
        SuperCamCentral* scc = GetSuperCamManager()->GetSCC( view );
        scc->SetIsInitialCamera(true);
    }
    GetGameplayManager()->Update( 0 );
    GetTriggerVolumeTracker()->Update( 0 );
}

//=============================================================================
// GameplayContext::OnStop
//=============================================================================
// Description: Comment
//
// Parameters:  ( ContextEnum nextContext )
//
// Return:      void 
//
//=============================================================================
void GameplayContext::OnStop( ContextEnum nextContext )
{
    GetPresentationManager()->OnGameplayStop();

    // turn off controller rumble, to comply w/ TCR/TRC standards
    //
    GetInputManager()->ToggleRumble( false );


    // Common to all playing contexts.
    //
    PlayingContext::OnStop( nextContext );
}

//=============================================================================
// GameplayContext::OnUpdate
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int elapsedTime )
//
// Return:      void 
//
//=============================================================================
void GameplayContext::OnUpdate( unsigned int elapsedTime )
{
#ifdef DEBUGWATCH
    mDebugOnUpdateDT = radTimeGetMicroseconds();
#endif

#ifndef FINAL
    if( CommandLineOptions::Get( CLO_PRINT_FRAMERATE ) )
    {
        //
        // Merasure the time taken to render frames 100-500
        //
        const unsigned int startFrame   = 200;
        const unsigned int endFrame     = 600;
        static unsigned int count = 0;
        ++count;
        static unsigned int totalTime = 0;
        totalTime += elapsedTime;

        if( count == startFrame )
        {
            totalTime = 0;
        }
        if( count == endFrame )
        {
            const unsigned int frames = endFrame - startFrame;
            float msPerFrame = totalTime / static_cast< float >( frames );
            rReleasePrintf( "====================================\n" );
            rReleasePrintf( "%d frames rendered in %dms\n", frames, totalTime );
            rReleasePrintf( "%fms per frame\n", msPerFrame );
            rReleasePrintf( "====================================\n" );
        }
    }

    static int frameCount = 60;
    frameCount--;
    if( frameCount == 0 )
    {
        HeapMgr()->ResetArtStats();
    }
#endif

#ifdef RAD_LINUX
    // Linux port: "tp<N>" test option: two seconds after loading, teleport the player to the level's teleport
    // destination N (CharacterManager::Teleport, which also loads the zones there)
    if( CommandLineOptions::GetDefaultTeleport() >= 0 )
    {
        static int s_teleportWait = 2000;
        static int s_relocateWait = -1;
        if( s_teleportWait > 0 && m_state != S_SUSPENDED && !m_PausedAllButPresentation &&
            !GetLoadingManager()->IsLoading() )
        {
            s_teleportWait -= static_cast< int >( elapsedTime );
            if( s_teleportWait <= 0 )
            {
                unsigned int which = (unsigned int)CommandLineOptions::GetDefaultTeleport();
                if( which < CharacterManager::GetNumTeleportDests() )
                {
                    fprintf( stderr, "tp: teleporting to \"%s\"\n", CharacterManager::GetTeleportDest( which ) );
                    CharacterManager::Teleport( which );
                    s_relocateWait = 2000;
                }
                else
                {
                    fprintf( stderr, "tp: the level has %u teleport destinations\n", CharacterManager::GetNumTeleportDests() );
                    s_relocateWait = 2000; // SHAR_TP_POS still applies (a point in the zones already loaded)
                }
            }
        }

        // SHAR_TP_POS="x,y,z": then to that point instead, once the destination's zones have loaded (two seconds
        // without loading: placed earlier, e.g. under valgrind, the player fell through ground not yet there)
        if( s_relocateWait > 0 && m_state != S_SUSPENDED && !m_PausedAllButPresentation )
        {
            if( GetLoadingManager()->IsLoading() )
            {
                s_relocateWait = 2000;
            }
            else
            {
                s_relocateWait -= static_cast< int >( elapsedTime );
                if( s_relocateWait <= 0 )
                {
                    const char* at = getenv( "SHAR_TP_POS" );
                    rmt::Vector pos;
                    Character* character = GetCharacterManager()->GetCharacter( 0 );
                    if( at && sscanf( at, "%f,%f,%f", &pos.x, &pos.y, &pos.z ) == 3 && character && !character->IsInCar() )
                    {
                        fprintf( stderr, "tp: moving to %.1f,%.1f,%.1f\n", pos.x, pos.y, pos.z );
                        character->RelocateAndReset( pos, 0 );
                    }
                }
            }
        }
    }

    // Linux port: SHAR_TP_INTERIOR_POS="x,y,z" (test option): once inside an interior (e.g. after tapping the action
    // button at its door), move there when nothing has loaded for two seconds, e.g. to an interior's clothes shop
    {
        static const char* s_interiorAt = getenv( "SHAR_TP_INTERIOR_POS" );
        static int s_interiorWait = 2000;
        if( s_interiorAt != NULL && s_interiorWait > 0 && m_state != S_SUSPENDED && !m_PausedAllButPresentation &&
            GetInteriorManager()->IsInside() && !GetInteriorManager()->IsEntering() )
        {
            if( GetLoadingManager()->IsLoading() )
            {
                s_interiorWait = 2000;
            }
            else
            {
                s_interiorWait -= static_cast< int >( elapsedTime );
                rmt::Vector pos;
                Character* character = GetCharacterManager()->GetCharacter( 0 );
                if( s_interiorWait <= 0 && character != NULL &&
                    sscanf( s_interiorAt, "%f,%f,%f", &pos.x, &pos.y, &pos.z ) == 3 )
                {
                    fprintf( stderr, "tp: moving inside to %.1f,%.1f,%.1f\n", pos.x, pos.y, pos.z );
                    character->RelocateAndReset( pos, 0 );
                }
            }
        }
    }

    // Linux port: "incar" test option: two seconds after loading, put the player in the mission's current car
    // (it teleports them to wherever the car is parked; skipped inside an interior)
    if( CommandLineOptions::Get( CLO_START_IN_CAR ) )
    {
        static int s_inCarWait = 2000;
        if( s_inCarWait > 0 && m_state != S_SUSPENDED && !m_PausedAllButPresentation &&
            !GetLoadingManager()->IsLoading() )
        {
            s_inCarWait -= static_cast< int >( elapsedTime );
            if( s_inCarWait <= 0 )
            {
                Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
                Vehicle* car = GetGameplayManager()->GetCurrentVehicle();
                if( GetInteriorManager()->IsInside() )
                {
                    fprintf( stderr, "incar: the player is in an interior, not moved\n" );
                }
                else if( avatar != NULL && car != NULL && !avatar->IsInCar() )
                {
                    GetAvatarManager()->PutCharacterInCar( avatar->GetCharacter(), car );
                }
            }
        }
    }
#endif

    // hack for yousuf
    if( mSlowMoHack )
    {
        //elapsedTime = 1;
        elapsedTime = 2;
    }

    if( !m_PausedAllButPresentation )
    {
        float timeins = (float)(elapsedTime) / 1000.0f;

        BEGIN_PROFILE("GameplayManager");
        if ( m_state != S_SUSPENDED )
        {
            GetGameplayManager()->Update( elapsedTime );
        }
        END_PROFILE("GameplayManager");


        GetAvatarManager()->Update( timeins );

        GetFootprintManager()->Update( elapsedTime );

	    GetCharacterManager()->GarbageCollect( );

	    BEGIN_PROFILE( "Update Particles" );

	    GetParticleManager()->Update( elapsedTime );
        GetBreakablesManager()->Update( elapsedTime );
	    GetAnimEntityDSGManager()->Update( elapsedTime );

	    END_PROFILE( "Update Particles" );

    #ifdef DEBUGWATCH
        unsigned int t0 = radTimeGetMicroseconds();
    #endif 

    BEGIN_PROFILE("WorldPhysics");
        GetWorldPhysicsManager()->Update(elapsedTime);
    END_PROFILE("WorldPhysics");

        // ordering is important. Unless other parts of code change, we must call
        // this before WorldPhysManager::Update() because PedestrianManager
        // sets the flags for all characters to be updated in WorldPhys Update 
        PedestrianManager::GetInstance()->Update( elapsedTime );

    #ifdef DEBUGWATCH
        mDebugTimeDelta = elapsedTime;
        mDebugPhysTiming = radTimeGetMicroseconds()-t0 ;
    #endif 

    BEGIN_PROFILE("TriggerVolumeTracker");
        GetTriggerVolumeTracker()->Update( elapsedTime );
    END_PROFILE("TriggerVolumeTracker");

    BEGIN_PROFILE("TrafficManager");
        TrafficManager::GetInstance()->Update( elapsedTime );
    END_PROFILE("TrafficManager");

    BEGIN_PROFILE("ActorManager");
        ActorManager::GetInstance()->Update( elapsedTime );
    END_PROFILE("ActorManager");

    BEGIN_PROFILE("InteriorManager");
        GetInteriorManager()->Update( elapsedTime );
    END_PROFILE("InteriorManager");

    BEGIN_PROFILE("SkidmarkManager");
        GetSkidmarkManager()->Update( elapsedTime );
    END_PROFILE("SkidmarkManager");

    BEGIN_PROFILE( "CoinManager" );
        GetCoinManager()->Update( elapsedTime );
        GetSparkleManager()->Update( elapsedTime );
    END_PROFILE( "CoinManager" );

    BEGIN_PROFILE( "HitnRunManager" );
        GetHitnRunManager()->Update( elapsedTime );
    END_PROFILE( "HitnRunManager" );
    }

#ifdef RAD_DEMO
    GetGameplayManager()->UpdateIdleTime( elapsedTime );
#endif

    BEGIN_PROFILE("PresentationManager");
       GetPresentationManager()->Update( elapsedTime );
    END_PROFILE("PresentationManager");

    // Common to all playing contexts.
    //
    PlayingContext::OnUpdate( elapsedTime );
#ifdef RAD_LINUX
    // Linux port: finishes a save started mid-level (the developer console's save); the original only updates
    // the game data manager in the bootup, front end and pause contexts
    GetGameDataManager()->Update( elapsedTime );
#endif
#ifdef DEBUGWATCH
    mDebugOnUpdateDT = radTimeGetMicroseconds()-mDebugOnUpdateDT;
#endif
}

//=============================================================================
// GameplayContext::OnSuspend
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void GameplayContext::OnSuspend()
{
    // Common to all playing contexts.
    //
    PlayingContext::OnSuspend();
}

//=============================================================================
// GameplayContext::OnResume
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void GameplayContext::OnResume()
{
    // Common to all playing contexts.
    //
    PlayingContext::OnResume();
}

//=============================================================================
// GameplayContext::PauseAllButPresentation
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void GameplayContext::PauseAllButPresentation( const bool pause )
{
    m_PausedAllButPresentation = pause;
}

