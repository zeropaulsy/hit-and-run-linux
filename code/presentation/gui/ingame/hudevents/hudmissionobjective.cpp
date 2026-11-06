//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   HudMissionObjective
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/02/05		TChu		Created
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/ingame/hudevents/hudmissionobjective.h>
#include <presentation/gui/ingame/guimanageringame.h>
#include <presentation/gui/ingame/guiscreenhud.h>
#include <presentation/gui/utility/specialfx.h>

#include <mission/gameplaymanager.h>
#include <mission/mission.h>
#include <mission/missionstage.h>

// Scrooby
#include <Group.h>
#include <Page.h>
#include <Sprite.h>

// Pure3D
#include <p3d/utility.hpp>
#include <p3d/sprite.hpp>
#include <p3d/displayaspect.hpp>

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

const int HUD_ICON_SLIDE_DISTANCE = 310; // vertical distance in pixels

// Linux port: the icon rests at the top of the page, anchored to the top edge of a window taller than 4:3 (Wide HUD),
// which moves it up. It starts beside the message, so until it slides up it is moved back down by as much (in pixels
// of the 640x480 page), and the slide covers it too.
static const float PAGE_HEIGHT = 480.0f;

static int GetIconPopUpOffset()
{
    return -static_cast< int >( p3d::GetEdgeShiftY() * PAGE_HEIGHT );
}

//===========================================================================
// Public Member Functions
//===========================================================================

HudMissionObjective::HudMissionObjective( Scrooby::Page* pPage )
:   HudEventHandler( pPage->GetGroup( "MissionObjective" ) ),
    m_currentSubState( STATE_ICON_POP_UP ),
    m_missionIcon( NULL ),
    m_missionIconImage( NULL ),
    m_messageID( 0 )
{
    rAssert( pPage != NULL );

    m_missionIcon = pPage->GetSprite( "ObjectiveIcon" );
    rAssert( m_missionIcon != NULL );

    // Linux port: the group also holds what stays in the 4:3 area (the message), so not the group but only the icon is
    // anchored (HudEventHandler anchored the group): to its side, and to the top, where it rests
    // (GetIconPopUpOffset), not to the bottom, where it lies.
    m_drawableGroup->SetEdgeAnchor( 0, 0 );
    m_missionIcon->AnchorToPageSide();
    m_missionIcon->SetEdgeAnchor( m_missionIcon->GetEdgeAnchorX(), 1 );

    m_iconTranslator.SetDrawable( m_missionIcon );
    m_iconTranslator.SetStartOffscreenBottom( m_missionIcon );
    m_iconTranslator.SetFrequency( 5.0f );
    m_iconTranslator.SetTimeInterval( 500.0f );

    bool isIconFound = this->UpdateIcon();
    if( isIconFound )
    {
        this->OnStart();

        m_missionIcon->ResetTransformation();
        m_missionIcon->ScaleAboutCenter( MISSION_ICON_SCALE );
        m_missionIcon->Translate( 0, HUD_ICON_SLIDE_DISTANCE );

        m_currentSubState = STATE_IDLE;
    }
}

HudMissionObjective::~HudMissionObjective()
{
    if( m_missionIconImage != NULL )
    {
        m_missionIconImage->Release();
        m_missionIconImage = NULL;
    }
}

void
HudMissionObjective::Start()
{
    this->OnStart();

    m_currentSubState = STATE_ICON_POP_UP;

    bool isIconFound = this->UpdateIcon();
    if( isIconFound )
    {
        m_iconTranslator.Reset();
        m_iconTranslator.Activate();
    }
    else
    {
        m_iconTranslator.Deactivate();
    }
}

void
HudMissionObjective::Stop()
{
    CGuiScreenHud* currentHud = GetCurrentHud();
    if( currentHud != NULL )
    {
        currentHud->DisplayMessage( false );
    }

    rAssert( m_missionIcon != NULL );
    m_missionIcon->SetRawSprite( NULL );
    m_missionIcon->SetVisible( false );

    if( m_missionIconImage != NULL )
    {
        m_missionIconImage->Release();
        m_missionIconImage = NULL;
    }

    this->OnStop();
}

void
HudMissionObjective::Update( float elapsedTime )
{
    if( m_currentState == STATE_RUNNING )
    {
        m_elapsedTime += elapsedTime;

        switch( m_currentSubState )
        {
            case STATE_ICON_POP_UP:
            {
                rAssert( m_missionIcon != NULL );
                m_missionIcon->ResetTransformation();
                m_missionIcon->ScaleAboutCenter( MISSION_ICON_SCALE );

                if( !m_iconTranslator.IsDone() )
                {
                    m_iconTranslator.Update( elapsedTime );
                }
                else
                {
                    m_iconTranslator.Deactivate();

                    CGuiScreenHud* currentHud = GetCurrentHud();
                    rAssert( currentHud != NULL );
                    currentHud->DisplayMessage( true, m_messageID );

                    // advance to next sub state
                    //
                    m_elapsedTime = 0.0f;
                    m_currentSubState++;
                }

                // Linux port: beside the message, see GetIconPopUpOffset
                m_missionIcon->Translate( 0, GetIconPopUpOffset() );

                break;
            }
            case STATE_ICON_DISPLAY_HOLD:
            {
                const float DISPLAY_HOLD_TIME = 3000.0f;

                if( m_elapsedTime < DISPLAY_HOLD_TIME )
                {
                    // do nothing
                }
                else
                {
                    // advance to next sub state
                    //
                    m_elapsedTime = 0.0f;
                    m_currentSubState++;
                }

                break;
            }
            case STATE_ICON_SLIDE_UP:
            {
                const float SLIDE_UP_TIME = 200.0f;

                rAssert( m_missionIcon != NULL );
                m_missionIcon->ResetTransformation();
                m_missionIcon->ScaleAboutCenter( MISSION_ICON_SCALE );

                if( m_elapsedTime < SLIDE_UP_TIME )
                {
                    // Linux port: from where it was beside the message (GetIconPopUpOffset)
                    int offset = GetIconPopUpOffset();
                    int translateY = offset + (int)( (m_elapsedTime / SLIDE_UP_TIME) *
                                                     ( HUD_ICON_SLIDE_DISTANCE - offset ) );
                    m_missionIcon->Translate( 0, translateY );
                }
                else
                {
                    m_missionIcon->Translate( 0, HUD_ICON_SLIDE_DISTANCE );

                    // advance to next sub state
                    //
                    m_elapsedTime = 0.0f;
                    m_currentSubState++;
                }

                break;
            }
            case STATE_IDLE:
            {
                // do nothing
                //
                break;
            }
            case NUM_SUB_STATES:
            {
                rAssertMsg( false, "We shouldn't be here; last sub-state should be IDLE." );

                break;
            }
            default:
            {
                rAssertMsg( false, "Unhandled sub-state!" );

                break;
            }
        }
    }
}

bool
HudMissionObjective::UpdateIcon()
{
    tSprite* pSprite = NULL;

    Mission* currentMission = GetGameplayManager()->GetCurrentMission();
    if( currentMission != NULL )
    {
        MissionStage* currentStage = currentMission->GetCurrentStage();
        if( currentStage != NULL )
        {
            const char* iconName = currentStage->GetHUDIcon();
            if( iconName[ 0 ] != '\0' )
            {
                pSprite = p3d::find<tSprite>( iconName );
                rTuneWarningMsg( pSprite != NULL, "Can't find HUD icon for mission presentation!" );
            }
        }
    }

    rAssert( m_missionIcon != NULL );
    m_missionIcon->SetVisible( pSprite != NULL );
    m_missionIcon->SetRawSprite( pSprite, true );

    if( pSprite != NULL )
    {
        tRefCounted::Assign( m_missionIconImage, pSprite );
    }

    return (pSprite != NULL);
}

