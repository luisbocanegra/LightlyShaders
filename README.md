 ![default](https://github.com/a-parhom/LightlyShaders/blob/master/screenshot.png)
 This is a fork of Luwx's LightlyShaders (https://github.com/Luwx/LightlyShaders), which in turn is a fork of [ShapeCorners](https://sourceforge.net/projects/shapecorners/). 

 This fork additionally implements two options:
 - an option to invert the color of the outline around the window; 
 - an option to add a dark outer border.

 Also it fixes the infamous [bug](https://bugs.kde.org/show_bug.cgi?id=395725) with blurred corners, though in a somehow hacky way. 
 
 # Warnings:

 ## 1. This effect is basically a hack!
Due to the changes introduced in Plasma 5.23 there is no way to draw original shadows under rounded corners any more. 

In order to work around that, this fork uses a hack that tries to restore the shadow in the cut out regions based on the data from the closest regions with shadows. 

Because of this it may work differently with different themes, corner radiuses or shadow settings. **Your mileage may vary.**

 ## 2. This effect can be resource-hungry!
Thanks to recent changes the performance of this plugin has improved. But depending on your hardware, you still can have performance hit. See [issue #6](https://github.com/a-parhom/LightlyShaders/issues/6). 


 # Dependencies:
 
Plasma >= 5.23.
 
Debian based (Ubuntu, Kubuntu, KDE Neon):
```
sudo apt install git cmake g++ gettext extra-cmake-modules qttools5-dev libqt5x11extras5-dev libkf5configwidgets-dev libkf5crash-dev libkf5globalaccel-dev libkf5kio-dev libkf5notifications-dev kinit-dev kwin-dev 
```
Fedora based
```
sudo dnf install git cmake gcc-c++ extra-cmake-modules qt5-qttools-devel qt5-qttools-static qt5-qtx11extras-devel kf5-kconfigwidgets-devel kf5-kcrash-devel kf5-kguiaddons-devel kf5-kglobalaccel-devel kf5-kio-devel kf5-ki18n-devel kf5-knotifications-devel kf5-kinit-devel kwin-devel qt5-qtbase-devel libepoxy-devel
```

# Manual installation
```
git clone https://github.com/a-parhom/LightlyShaders

cd LightlyShaders; mkdir qt5build; cd qt5build; cmake ../ -DCMAKE_INSTALL_PREFIX=/usr -DQT5BUILD=ON && make && sudo make install && (kwin_x11 --replace &)
```
 
 
 
