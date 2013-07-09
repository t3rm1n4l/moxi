Summary:      Moxi - ZBase proxy server
Name:         moxi
Version:      %{?version}
Release:      1
License:      Zynga
Group:        Development/Languages
BuildRoot:    %{?buildpath}

%description
Moxi is a proxy server for ZBase

%post
chmod a+x /opt/moxi/moximon.sh
chmod a+x /etc/init.d/moxi
/sbin/ldconfig /opt/moxi

%files
/opt/moxi/*
/etc/init.d/moxi

