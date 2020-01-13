Name:       nemo-qtmultimedia-plugins-gstvideotexturebackend
Summary:    QtMultimedia QML VideoOutput backend for GStreamer NemoVideoTexture interface
Version:    0.0.0
Release:    1
Group:      System/Libraries
License:    BSD
URL:        https://git.sailfishos.org/mer-core/nemo-qtmultimedia-plugins/
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(nemo-gstreamer-interfaces-1.0)
BuildRequires:  qt5-qtmultimedia-gsttools

%description
%{summary}.

%prep
%setup -q -n %{name}-%{version}

%build

%qmake5 

make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

%files
%defattr(-,root,root,-)
%{_libdir}/qt5/plugins/video/declarativevideobackend/libgstnemovideotexturebackend.so
