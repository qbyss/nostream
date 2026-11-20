#ifndef EVO_FAVORITES_SCREEN_HPP
#define EVO_FAVORITES_SCREEN_HPP

#include "evo/screens/StatefulScreen.hpp"

namespace evo {

class FavoritesScreen : public StatefulScreen {
public:
    FavoritesScreen();
    ~FavoritesScreen() override = default;

    ScreenId getScreenId() const override { return ScreenId::Favorites; }
    void onEnter() override;
    bool handleInput(uint32_t, uint32_t, uint32_t) override { return false; }
    void render(uint32_t*, int, int) override {}
};

} // namespace evo

#endif // EVO_FAVORITES_SCREEN_HPP
