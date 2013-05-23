Summary:      Moxi - Membase proxy server 
Name:         moxi
Version:      %{?version}
Release:      1
License:      Zynga
Group:        Development/Languages
BuildRoot:    %{?buildpath}

Requires(rpmlib): rpmlib(CompressedFileNames) <= 3.0.4-1 rpmlib(PayloadFilesHavePrefix) <= 4.0-1

%description
Moxi is a proxy server for membase

%post
chmod a+x /opt/moxi/moximon.sh
chmod a+x /etc/init.d/moxi
/sbin/ldconfig /opt/moxi

if [ -e /etc/rsyslog.d ];
then
    echo  ':syslogtag,contains,"moxi" /var/log/moxi.log' > /etc/rsyslog.d/moxi.conf
fi
/etc/init.d/rsyslog restart

%files
/opt/moxi/*
/etc/init.d/moxi


