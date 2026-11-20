#include "evo/screens/FavoritesScreen.hpp"
#include "evo/screens/BrowserScreen.hpp"
#include "evo/Application.hpp"

namespace evo {

FavoritesScreen::FavoritesScreen()
    : StatefulScreen("FavoritesScreen") {
}

void FavoritesScreen::onEnter() {
    StatefulScreen::onEnter();
    if (auto sm = Application::getInstance().getScreenManager()) {
        if (auto bs = dynamic_cast<BrowserScreen*>(sm->getScreen(ScreenId::UsbBrowser))) {
            bs->setSource(3);
        }
        sm->navigateTo(ScreenId::UsbBrowser);
    }
}


} // namespace evo
