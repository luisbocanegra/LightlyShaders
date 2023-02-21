# Support Ukraine:
  - Via United24 platform (the initiative of the President of Ukraine):
    - [One click donation (credit card, bank transfer or crypto)](https://u24.gov.ua/)
  - Via National Bank of Ukraine:
    - [Ukrainian army](https://bank.gov.ua/en/about/support-the-armed-forces)
    - [Humanitarian aid to Ukraine](https://bank.gov.ua/en/about/humanitarian-aid-to-ukraine)

# LightlyShaders v2.1
 This is a fork of Luwx's [LightlyShaders](https://github.com/Luwx/LightlyShaders), which in turn is a fork of [ShapeCorners](https://sourceforge.net/projects/shapecorners/).  

 It works correctly with stock Plasma effects:

 ![gif](https://github.com/a-parhom/LightlyShaders/blob/v2.0/lightly_shaders_2.0.gif)

 ![default](https://github.com/a-parhom/LightlyShaders/blob/v2.0/screenshot.png)

# Warning:

## On Wayland corners may have "korner bug" when using blur due to the lack of API for adjusting blur region on Plasma 5.27.

## This version heavily relies on window decorations, that correctly work with Plasma 5.25 "korner bug" fix!
Currently I can confirm this effect correctly works with **SierraBreezeEnhanced** or default **Breeze** (though Breeze has hardcoded corner radius). You have to make sure, that your radius settings in window decorations match with settings in LightlyShaders.  

## Alternatively you can try my new project: [RoundedSBE](https://github.com/a-parhom/RoundedSBE)
This is a fork of **SierraBreezeEnhanced** window decoration with a built-in corner-rounding effect **CornersShader** - a simplified version of LightlyShaders, integrated with RoundedSBE's configuration. It is in it's early stage, may have bugs and/or missing features.


# Dependencies:
 
Plasma >= 5.27 (5.27.1 to fix issue #87).
 
Debian based (Ubuntu, Kubuntu, KDE Neon):
```
sudo apt install git cmake g++ gettext extra-cmake-modules qttools5-dev libqt5x11extras5-dev libkf5configwidgets-dev libkf5crash-dev libkf5globalaccel-dev libkf5kio-dev libkf5notifications-dev kinit-dev kwin-dev 
```
Fedora based
```
sudo dnf install git cmake gcc-c++ extra-cmake-modules qt5-qttools-devel qt5-qttools-static qt5-qtx11extras-devel kf5-kconfigwidgets-devel kf5-kcrash-devel kf5-kguiaddons-devel kf5-kglobalaccel-devel kf5-kio-devel kf5-ki18n-devel kf5-knotifications-devel kf5-kinit-devel kwin-devel qt5-qtbase-devel libepoxy-devel
```
Arch based
```
sudo pacman -S git make cmake gcc gettext extra-cmake-modules qt5-tools qt5-x11extras kcrash kglobalaccel kde-dev-utils kio knotifications kinit kwin
```
OpenSUSE based
```
sudo zypper install git cmake gcc-c++ extra-cmake-modules libqt5-qttools-devel libqt5-qtx11extras-devel kconfigwidgets-devel kcrash-devel kguiaddons-devel kglobalaccel-devel kio-devel ki18n-devel knotifications-devel kinit-devel kwin5-devel libQt5Gui-devel libQt5OpenGL-devel libepoxy-devel
```

# Manual installation
```
git clone https://github.com/a-parhom/LightlyShaders

cd LightlyShaders;

mkdir qt5build; cd qt5build; cmake ../ -DCMAKE_INSTALL_PREFIX=/usr && make && sudo make install && (kwin_x11 --replace &)
```

## Note
After some updates of Plasma this plugin may need to be recompiled in order to work with changes introduced to KWin.
 
