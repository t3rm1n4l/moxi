Summary:      Moxi - ZBase proxy server
Name:         moxi
Version:      %{?version}
Release:      1
Group:        Development/Languages
BuildRoot:    %{?buildpath}

%description
Moxi is a proxy server for ZBase

%post
chmod a+x /opt/moxi/moximon.sh
chmod a+x /etc/init.d/moxi
/sbin/ldconfig /opt/moxi

if [ -e /etc/rsyslog.d ];
then
    echo  ':syslogtag,contains,"moxi" /var/log/moxi.log' > /etc/rsyslog.d/moxi.conf
/etc/init.d/rsyslog restart
elif [ -e /etc/syslog-ng/syslog-ng.conf ];
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


