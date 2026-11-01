//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenMissionSuccess
//
// Description: 
//              
//
// Authors:     Ian Gipson
//
// Revisions		Date			Author	    Revision
//                  2003/04/07      igipson     Created for SRR2
//
//===========================================================================

#ifndef GUISCREENMISSIONSUCCESS_H
#define GUISCREENMISSIONSUCCESS_H

//===========================================================================
// Nested Includes
//===========================================================================
#include <presentation/gui/ingame/guiscreenmissionload.h>

//===========================================================================
// Forward References
//===========================================================================

//===========================================================================
// Interface Definitions
//===========================================================================
class CGuiScreenMissionSuccess : 
    public CGuiScreenMissionBase
{
public:
    CGuiScreenMissionSuccess( Scrooby::Screen* pScreen, CGuiEntity* pParent );
	virtual void HandleMessage( eGuiMessage message, 
			                    GuiParam param1 = 0,
								GuiParam param2 = 0 );
    virtual void InitIntro();
#ifdef RAD_LINUX
    // Linux port: the mission that brought up the screen (mods' end bitmaps); the current mission has moved on
    // by InitIntro
    static void SetModMissionName( const char* name );
#endif
protected:
    virtual void OutroDone();
private:
    Scrooby::Text* m_PattyAndSelmaTitle;
    Scrooby::Text* m_PattyAndSelmaInfo;
};

#endif // GUISCREENMISSIONSUCCESS_H
