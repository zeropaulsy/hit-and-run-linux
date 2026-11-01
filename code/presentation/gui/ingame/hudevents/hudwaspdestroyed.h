//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   HudWaspDestroyed
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/02/05		TChu		Created
//
//===========================================================================

#ifndef HUDWASPDESTROYED_H
#define HUDWASPDESTROYED_H

//===========================================================================
// Nested Includes
//===========================================================================
#include <presentation/gui/ingame/hudevents/hudeventhandler.h>

//===========================================================================
// Forward References
//===========================================================================

namespace Scrooby
{
    class Page;
    class Text;
}
#ifdef RAD_LINUX
class UnicodeString;
#endif

//===========================================================================
// Interface Definitions
//===========================================================================

class HudWaspDestroyed : public HudEventHandler
{
public:
    HudWaspDestroyed( Scrooby::Page* pPage );
    virtual ~HudWaspDestroyed();

    virtual void Start();
    virtual void Stop();
    virtual void Update( float elapsedTime );

    void SetGagInsteadOfWasp( bool isGagInsteadOfWasp );

#ifdef RAD_LINUX
    // Linux port: Mod Launcher CustomRewardQuestSupport's quest complete message (replaces a wasp / gag one)
    void StartModMessage( UnicodeString& text );
    bool IsShowingModMessage() const { return IsActive() && m_isModMessage; }
#endif

private:
    enum eSubState
    {
        STATE_WASP_TRANSITION_IN,
        STATE_WASP_DISPLAY_HOLD,
        STATE_WASP_TRANSITION_OUT,

        NUM_SUB_STATES
    };

    unsigned int m_currentSubState;

    Scrooby::Text* m_itemsComplete;

    // re-use this event handler for gags
    //
    bool m_isGagInsteadOfWasp : 1;

#ifdef RAD_LINUX
    bool m_isModMessage;
    int m_modMessageIndex;  // the hard-coded string added to WaspsComplete for it, -1: none
#endif

};

inline void HudWaspDestroyed::SetGagInsteadOfWasp( bool isGagInsteadOfWasp )
{
    m_isGagInsteadOfWasp = isGagInsteadOfWasp;
}

#endif // HUDWASPDESTROYED_H
