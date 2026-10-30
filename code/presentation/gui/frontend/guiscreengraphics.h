//===========================================================================
// Component:   CGuiScreenGraphics
//
// Description: Linux port: Options > Graphics (front end and pause menu): anti-aliasing, the user's shaders
//              (shaders/) and the renderer. Built on the Display screen's page: its first three rows get
//              copies of their texts with this screen's labels and values, shown while this screen is up.
//
//===========================================================================

#ifndef GUISCREENGRAPHICS_H
#define GUISCREENGRAPHICS_H

//===========================================================================
// Nested Includes
//===========================================================================
#include <presentation/gui/guiscreen.h>

//===========================================================================
// Forward References
//===========================================================================
class CGuiMenu;

//===========================================================================
// Interface Definitions
//===========================================================================
class CGuiScreenGraphics : public CGuiScreen
{
public:
    // pScreen: the Display screen (front end) or PauseDisplay (in game, inGame = true: Start resumes the game)
    CGuiScreenGraphics( Scrooby::Screen* pScreen, CGuiEntity* pParent, bool inGame );
    virtual ~CGuiScreenGraphics();

	virtual void HandleMessage( eGuiMessage message,
			                    GuiParam param1 = 0,
								GuiParam param2 = 0 );

    virtual CGuiMenu* HasMenu() { return m_pMenu; }

    // Shows this screen's rows on the Display page (graphics = true) or puts the Display screen's back (its
    // InitIntro calls this: not done in this screen's outro, which would show them while fading out)
    void ShowPage( bool graphics );

protected:
    void InitIntro();
	void InitRunning();
	void InitOutro();

private:
    enum eMenuItem
    {
        MENU_ITEM_ANTIALIASING,
        MENU_ITEM_SHADERS,
        MENU_ITEM_RENDERER,

        NUM_MENU_ITEMS
    };

    CGuiMenu* m_pMenu;
    bool m_inGame;
    bool m_changed;

    // the Display page's rows (label, value, arrows) and this screen's copies of the texts
    Scrooby::Text* m_displayLabels[ NUM_MENU_ITEMS ];
    Scrooby::Text* m_displayValues[ NUM_MENU_ITEMS ];
    Scrooby::Sprite* m_arrows[ NUM_MENU_ITEMS ][ 2 ];
    Scrooby::Text* m_labels[ NUM_MENU_ITEMS ];
    Scrooby::Text* m_values[ NUM_MENU_ITEMS ];
    Scrooby::Group* m_gamma;            // hidden here
    Scrooby::Text* m_applyChanges;      // likewise: the settings apply at once
};

// The Options menus' "Graphics" item: a copy of the text `after` placed below it; the texts in `below` move down a
// row and the whole list up half a row (stays centred). Returns the new text.
Scrooby::Text* AddGraphicsMenuItem( Scrooby::Group* menu, const char* after, const char* const* below, int numBelow );

#endif // GUISCREENGRAPHICS_H
