//===========================================================================
// Copyright (C) 2003 Radical Entertainment Ltd.  All rights reserved.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/02/26		TChu		Created
//
//===========================================================================

#ifndef GUITEXTBIBLE_H
#define GUITEXTBIBLE_H

//===========================================================================
// Nested Includes
//===========================================================================

#include <p3d/plat_types.hpp>

// Scrooby
//
#include <Enums.h>

//===========================================================================
// External Constants
//===========================================================================

const Scrooby::XLLanguage SRR2_LANGUAGE[] =
{
    Scrooby::XL_ENGLISH,
    Scrooby::XL_FRENCH,
    Scrooby::XL_GERMAN,
//    Scrooby::XL_ITALIAN,
    Scrooby::XL_SPANISH,

    Scrooby::XL_LAST_LANGUAGE
};

const int NUM_SRR2_LANGUAGES =
    sizeof( SRR2_LANGUAGE ) / sizeof( SRR2_LANGUAGE[ 0 ] ) - 1;

//===========================================================================
// Forward References
//===========================================================================

namespace Scrooby
{
    class TextBible;
}

//===========================================================================
// Interface Definitions
//===========================================================================

class CGuiTextBible
{
public:
    CGuiTextBible();
    virtual ~CGuiTextBible();

    // update reference to Scrooby text bible
    //
    void SetTextBible( const char* textBible );

    // get localized text for string ID
    //
    static P3D_UNICODE* GetLocalizedText( const char* stringID );

    // get/set current locale language
    //
#ifdef RAD_LINUX
    // Linux port: dialogue = false keeps the dialogue cement file (the developer console's lang, mid-level)
    static void SetCurrentLanguage( const Scrooby::XLLanguage language, bool dialogue = true );
#else
    static void SetCurrentLanguage( const Scrooby::XLLanguage language );
#endif
    static Scrooby::XLLanguage GetCurrentLanguage();
#ifdef RAD_LINUX
    // Linux port: the language from simpsons.ini (language=), set before the front end and the sound start; only
    // records it (CGuiSystem::Init and the sound manager apply it)
    static void SetStartupLanguage( const Scrooby::XLLanguage language ) { s_currentLanguage = s_startupLanguage = language; }
    // what the player chose: speech, movies and simpsons.ini use this; the console's lang only changes the text
    static Scrooby::XLLanguage GetStartupLanguage() { return s_startupLanguage; }
    // the file is there, on disc or in a mod
    static bool GameFileExists( const char* filename );
#endif
    static bool IsTextBibleLoaded();

private:
    static Scrooby::TextBible* s_textBible;
    static Scrooby::XLLanguage s_currentLanguage;
#ifdef RAD_LINUX
    static Scrooby::XLLanguage s_startupLanguage;
#endif

};

inline P3D_UNICODE* GetTextBibleString( const char* stringID )
{
    return CGuiTextBible::GetLocalizedText( stringID );
}

#endif // GUITEXTBIBLE_H
