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
/etc/init.d/rsyslog restart
else if [ -e /etc/syslog-ng/syslog-ng.conf ];
then
    sed -i '/_moxi/d' /etc/syslog-ng/syslog-ng.conf

    cat <<EOF >> /etc/syslog-ng/syslog-ng.conf

destination d_moxi { file("/var/log/moxi.log" owner("root") group ("root") perm(0644) ); };
filter f_moxi { match('moxi'); };
log { source(s_sys); filter(f_moxi); destination(d_mb_backup); };
EOF
    /etc/init.d/syslog-ng restart
fi

%files
/opt/moxi/*
/etc/init.d/moxi


