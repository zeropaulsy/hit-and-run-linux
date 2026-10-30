//===========================================================================
// Copyright (C) 2003 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenDisplay
//
// Description: Implementation of the CGuiScreenDisplay class.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/06/16      TChu        Created for SRR2
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/frontend/guiscreendisplay.h>
#include <presentation/gui/guimenu.h>
#ifdef RAD_LINUX
#include <presentation/gui/frontend/guiscreengraphics.h>
#include <presentation/gui/guimanager.h>
#endif

#include <data/config/gameconfigmanager.h>
#include <main/win32platform.h>
#include <memory/srrmemory.h>
#include <render/RenderFlow/renderflow.h>

#include <raddebug.hpp> // Foundation
#include <Screen.h>
#include <Page.h>
#include <Group.h>
#include <Text.h>

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================


const char* DISPLAY_MENU_ITEMS[] =
{
    "Resolution",
    "ColourDepth",
    "DisplayMode",
    "Gamma",
    "ApplyChanges",

    ""
};

const float SLIDER_ICON_SCALE = 0.5f;

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenDisplay::CGuiScreenDisplay
//===========================================================================
// Description: Constructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenDisplay::CGuiScreenDisplay
(
    Scrooby::Screen* pScreen,
    CGuiEntity* pParent
)
:   CGuiScreen( pScreen, pParent, GUI_SCREEN_ID_DISPLAY ),
    m_pMenu( NULL ),
    m_changedGamma( false )
{
MEMTRACK_PUSH_GROUP( "CGuiScreenDisplay" );
#ifdef RAD_LINUX
    m_numResolutions = 0;
    m_pResolutionText = NULL;
#endif
    // Retrieve the Scrooby drawing elements.
    //
    Scrooby::Page* pPage = m_pScroobyScreen->GetPage( "Display" );
    rAssert( pPage != NULL );

    // Create a menu.
    //
    m_pMenu = new CGuiMenu( this, NUM_MENU_ITEMS );
    rAssert( m_pMenu != NULL );

    // Add menu items
    //
    char itemName[ 32 ];

    for( int i = 0; i < MENU_ITEM_GAMMA; i++ )
    {
        Scrooby::Group* group = pPage->GetGroup( DISPLAY_MENU_ITEMS[ i ] );
        rAssert( group != NULL );

        sprintf( itemName, "%s_Value", DISPLAY_MENU_ITEMS[ i ] );
        Scrooby::Text* pTextValue = group->GetText( itemName );

#ifdef RAD_LINUX
        if( i == MENU_ITEM_RESOLUTION )
        {
            m_pResolutionText = pTextValue;     // values filled in by BuildResolutionList()
        }
        else if( i == MENU_ITEM_DISPLAY_MODE && pTextValue->GetNumOfStrings() == 2 )
        {
            // Window, Full Screen (exclusive: the display switches to the resolution), + borderless fullscreen
            pTextValue->AddHardCodedString( "Borderless" );
        }
#endif

        sprintf( itemName, "%s_ArrowL", DISPLAY_MENU_ITEMS[ i ] );
        Scrooby::Sprite* pLArrow = group->GetSprite( itemName );

        sprintf( itemName, "%s_ArrowR", DISPLAY_MENU_ITEMS[ i ] );
        Scrooby::Sprite* pRArrow = group->GetSprite( itemName );

        m_pMenu->AddMenuItem( group->GetText( DISPLAY_MENU_ITEMS[ i ] ),
                              pTextValue,
                              NULL,
                              NULL,
                              pLArrow,
                              pRArrow,
                              SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED );
    }

    // Add the gamma slider
    Scrooby::Group* pgroup = pPage->GetGroup( "Gamma" );
    rAssert(pgroup  != NULL );

    Scrooby::Text* pText = pgroup->GetText( "Gamma" );

    Scrooby::Group* sliderGroup = pgroup->GetGroup( "Gamma_Slider" );
    rAssert( sliderGroup != NULL );

    sliderGroup->ResetTransformation();

    m_pMenu->AddMenuItem( pText,
                          NULL,
                          NULL,
                          sliderGroup->GetSprite( "Gamma_Slider" ),
                          NULL,
                          NULL,
                          SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED );

    m_pMenu->GetMenuItem( MENU_ITEM_GAMMA )->m_slider.m_type = Slider::HORIZONTAL_SLIDER_ABOUT_CENTER;

    Scrooby::Sprite* soundOnIcon = pgroup->GetSprite( "Gamma_Icon" );
    soundOnIcon->ScaleAboutCenter( SLIDER_ICON_SCALE );

    // Add the apply changes button

    pgroup = pPage->GetGroup( "Menu" );
    rAssert( pgroup != NULL );

    m_pMenu->AddMenuItem( pgroup->GetText( "ApplyChanges" ) );

MEMTRACK_POP_GROUP("CGuiScreenDisplay");
}


//===========================================================================
// CGuiScreenDisplay::~CGuiScreenDisplay
//===========================================================================
// Description: Destructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenDisplay::~CGuiScreenDisplay()
{
    if( m_pMenu != NULL )
    {
        delete m_pMenu;
        m_pMenu = NULL;
    }
}


//===========================================================================
// CGuiScreenDisplay::HandleMessage
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenDisplay::HandleMessage
(
	eGuiMessage message, 
	GuiParam param1,
	GuiParam param2 
)
{
    if( m_state == GUI_WINDOW_STATE_RUNNING )
    {
        switch( message )
        {
            case GUI_MSG_MENU_SELECTION_MADE:
            {
                switch( param1 )
                {
                    case MENU_ITEM_APPLY_CHANGES:
                    {
                        ApplySettings();
                        break;
                    }
                }
                break;
            }
            case GUI_MSG_MENU_SELECTION_VALUE_CHANGED:
            {
                rAssert( m_pMenu );
                GuiMenuItem* currentItem = m_pMenu->GetMenuItem( param1 );
                rAssert( currentItem );

                switch( param1 )
                {
                    case MENU_ITEM_GAMMA:
                    {
                        float gamma = 2 * currentItem->m_slider.m_value + 0.5f;
                        GetRenderFlow()->SetGamma( gamma );
                        m_changedGamma = true;

                        break;
                    }
                }
                break;
            }
        }

        // relay message to menu
        if( m_pMenu != NULL )
        {
            m_pMenu->HandleMessage( message, param1, param2 );
        }
    }

	// Propogate the message up the hierarchy.
	//
	CGuiScreen::HandleMessage( message, param1, param2 );
}

//===========================================================================
// CGuiScreenDisplay::InitIntro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenDisplay::InitIntro()
{
    // update settings
    //
    Win32Platform* plat = Win32Platform::GetInstance();

    Win32Platform::Resolution res = plat->GetResolution();
#ifdef RAD_LINUX
    BuildResolutionList();
    int resValue = 0;
    for( int k = 0; k < m_numResolutions; k++ )
    {
        if( m_resolutions[ k ] == res )
        {
            resValue = k;
        }
    }
    m_pMenu->SetSelectionValue( MENU_ITEM_RESOLUTION, resValue );
#else
    m_pMenu->SetSelectionValue( MENU_ITEM_RESOLUTION,
                                res );
#endif

    int bpp = plat->GetBPP();
    m_pMenu->SetSelectionValue( MENU_ITEM_COLOUR_DEPTH,
                                bpp == 16 ? 0: 1 );

    bool fullscreen = plat->IsFullscreen();
#ifdef RAD_LINUX
    m_pMenu->SetSelectionValue( MENU_ITEM_DISPLAY_MODE,
                                !fullscreen ? 0 : plat->IsBorderless() ? 2 : 1 );
#else
    m_pMenu->SetSelectionValue( MENU_ITEM_DISPLAY_MODE,
                                fullscreen ? 1 : 0 );
#endif

    GuiMenuItem* menuItem = m_pMenu->GetMenuItem( MENU_ITEM_GAMMA );
    rAssert( menuItem );
    menuItem->m_slider.SetValue( ( GetRenderFlow()->GetGamma() - 0.5f ) / 2.0f );

#ifdef RAD_LINUX
    // the Graphics screen shares this page: put this screen's rows back, and the selected row's value arrows
    CGuiScreenGraphics* graphics = static_cast< CGuiScreenGraphics* >( m_guiManager->FindWindowByID( GUI_SCREEN_ID_GRAPHICS ) );
    if( graphics != NULL )
    {
        graphics->ShowPage( false );
    }
    m_pMenu->Reset( m_pMenu->GetSelection() );
#endif
}


//===========================================================================
// CGuiScreenDisplay::InitRunning
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenDisplay::InitRunning()
{
}


//===========================================================================
// CGuiScreenDisplay::InitOutro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenDisplay::InitOutro()
{
    // Save the config if we've changed the gamma settings
    if( m_changedGamma )
    {
        GetGameConfigManager()->SaveConfigFile();
        m_changedGamma = false;
    }
}


//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------

//===========================================================================
// CGuiScreenDisplay::ApplySettings
//===========================================================================
// Description: Applies the current display settings to teh game. 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenDisplay::ApplySettings()
{
    // Retrieve the settings.
    //
#ifdef RAD_LINUX
    int resValue = m_pMenu->GetSelectionValue( MENU_ITEM_RESOLUTION );
    if( resValue < 0 || resValue >= m_numResolutions )
    {
        return;
    }
    Win32Platform::Resolution res = static_cast< Win32Platform::Resolution >( m_resolutions[ resValue ] );
#else
    Win32Platform::Resolution res = static_cast< Win32Platform::Resolution >( m_pMenu->GetSelectionValue( MENU_ITEM_RESOLUTION ) );
#endif

    int bpp = m_pMenu->GetSelectionValue( MENU_ITEM_COLOUR_DEPTH ) ? 32: 16;

#ifdef RAD_LINUX
    int displayMode = m_pMenu->GetSelectionValue( MENU_ITEM_DISPLAY_MODE );
    bool fullscreen = displayMode != 0;
    Win32Platform::GetInstance()->SetBorderless( displayMode == 2 );
#else
    bool fullscreen = m_pMenu->GetSelectionValue( MENU_ITEM_DISPLAY_MODE ) == 1;
#endif

    // Set the resolution.
    Win32Platform::GetInstance()->SetResolution( res, bpp, fullscreen );

    // Save the change to the config file.
    GetGameConfigManager()->SaveConfigFile();
    m_changedGamma = false;
}

#ifdef RAD_LINUX
//===========================================================================
// CGuiScreenDisplay::BuildResolutionList
//===========================================================================
// Description: Fills the resolution values with the sizes that fit the desktop (plus the current one),
//              smallest first. Display.pag's own strings are overwritten, extra ones added.
//
//===========================================================================
void CGuiScreenDisplay::BuildResolutionList()
{
    rAssert( m_pResolutionText != NULL );
    int current = Win32Platform::GetInstance()->GetResolution();

    int desktopW, desktopH;
    Win32Platform::GetDesktopSize( desktopW, desktopH );

    m_numResolutions = 0;
    int sizes[ MAX_MENU_RESOLUTIONS ][ 2 ];
    for( int r = 0; r < Win32Platform::GetNumResolutions() && m_numResolutions < MAX_MENU_RESOLUTIONS; r++ )
    {
        int w, h;
        Win32Platform::GetResolutionSize( static_cast< Win32Platform::Resolution >( r ), w, h );
        if( ( w > desktopW || h > desktopH ) && r != current )
        {
            continue;
        }

        // insertion sort by width, then height
        int k = m_numResolutions;
        while( k > 0 && ( sizes[ k - 1 ][ 0 ] > w || ( sizes[ k - 1 ][ 0 ] == w && sizes[ k - 1 ][ 1 ] > h ) ) )
        {
            sizes[ k ][ 0 ] = sizes[ k - 1 ][ 0 ];
            sizes[ k ][ 1 ] = sizes[ k - 1 ][ 1 ];
            m_resolutions[ k ] = m_resolutions[ k - 1 ];
            k--;
        }
        sizes[ k ][ 0 ] = w;
        sizes[ k ][ 1 ] = h;
        m_resolutions[ k ] = r;
        m_numResolutions++;
    }

    for( int k = 0; k < m_numResolutions; k++ )
    {
        char label[ 32 ];
        sprintf( label, "%d x %d", sizes[ k ][ 0 ], sizes[ k ][ 1 ] );
        if( k < m_pResolutionText->GetNumOfStrings() )
        {
            m_pResolutionText->SetString( k, label );
        }
        else
        {
            m_pResolutionText->AddHardCodedString( label );
        }
    }
    m_pMenu->SetSelectionValueCount( MENU_ITEM_RESOLUTION, m_numResolutions );
}
#endif
