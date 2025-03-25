#!/bin/sh

ss_bin="ss-redir"
ss_json_file="/tmp/ss-redir.json"
dns_conf="/etc/china_dns.conf"
ss_proc="/var/ss-redir"
gfwlist="/etc/storage/gfwlist/gfwlist_domain.txt"
ss_dns="tcp://8.8.8.8,tcp://8.8.4.4"

#/usr/bin/ss-redir -> /var/ss-redir -> /usr/bin/ss-orig-redir or /usr/bin/ssr-redir

ss_type="$(nvram get ss_type)" #0=ss;1=ssr

if [ "${ss_type:-0}" = "0" ]; then
	ln -sf /usr/bin/ss-orig-redir $ss_proc
elif [ "${ss_type:-0}" = "1" ]; then
	ss_protocol=$(nvram get ss_protocol)
	ss_proto_param=$(nvram get ss_proto_param)
	ss_obfs=$(nvram get ss_obfs)
	ss_obfs_param=$(nvram get ss_obfs_param)
	ln -sf /usr/bin/ssr-redir $ss_proc
fi

ss_local_port=$(nvram get ss_local_port)
ss_udp=$(nvram get ss_udp)
ss_server=$(nvram get ss_server)

ss_server_port=$(nvram get ss_server_port)
ss_method=$(nvram get ss_method)
ss_password=$(nvram get ss_key)
ss_mtu=$(nvram get ss_mtu)
ss_timeout=$(nvram get ss_timeout)

ss_mode=$(nvram get ss_mode) #0:global;1:chnroute;2:gfwlist
ss_router_proxy=$(nvram get ss_router_proxy)
ss_lower_port_only=$(nvram get ss_lower_port_only)

loger() {
	logger -st "$1" "$2"
}

get_arg_udp() {
	if [ "$ss_udp" = "1" ]; then
		echo "-u"
	fi
}

get_arg_out(){
	if [ "$ss_router_proxy" = "1" ]; then
		echo "-o"
	fi
}

get_wan_bp_list(){
	wanip="$(nvram get wan_ipaddr)"
	[ -n "$wanip" ] && [ "$wanip" != "0.0.0.0" ] && bp="-b $wanip" || bp=""
	if [ "$ss_mode" = "1" ]; then
		bp=${bp}" -B /etc/storage/chinadns/chnroute.txt"
	fi
	echo "$bp"
}

get_ipt_ext(){
	if [ "$ss_lower_port_only" = "1" ]; then
		echo '-e "--dport 22:1023"'
	elif [ "$ss_lower_port_only" = "2" ]; then
		echo '-e "-m multiport --dports 53,80,443"'
	fi
}

get_gfw_ext(){
	if [ "$ss_mode" = "2" ]; then
		echo '-g "gfw"'
	fi
}

func_start_ss_redir(){
	sh -c "$ss_bin -c $ss_json_file $(get_arg_udp) & "
	return $?
}

func_dl_list(){
	if [ ! -f "/tmp/chnlist.txt" ];then
		wget https://opt.cn2qq.com/opt-file/chinalist.txt -O /tmp/chnlist.txt
	fi
	if [ ! -f "/etc/storage/chinadns/gfw_add.txt" ];then
		touch /etc/storage/chinadns/gfw_add.txt
	fi
	if [ ! -f "/etc/storage/chinadns/chnlist.txt" ];then
		touch /etc/storage/chinadns/chnlist.txt
	fi
	if [ "$ss_mode" = "1" ]; then
		ipset -! restore <<-EOF
		create chnroute hash:net hashsize 64
		$(sed -e "s/^/add chnroute /" /etc/storage/chinadns/chnroute.txt 2>/dev/null)
EOF
	fi
	return 0
}

func_start_ss_rules(){
	ss-rules -f
	sh -c "ss-rules -s $ss_server -l $ss_local_port $(get_wan_bp_list) -d SS_SPEC_WAN_AC $(get_ipt_ext) $(get_arg_out) $(get_arg_udp) $(get_gfw_ext)"
	return $?
}

func_gen_ss_json(){
cat > "$ss_json_file" <<EOF
{
    "server": "$ss_server",
    "server_port": $ss_server_port,
    "password": "$ss_password",
    "method": "$ss_method",
    "timeout": $ss_timeout,
    "protocol": "$ss_protocol",
    "protocol_param": "$ss_proto_param",
    "obfs": "$ss_obfs",
    "obfs_param": "$ss_obfs_param",
    "local_address": "0.0.0.0",
    "local_port": $ss_local_port,
    "mtu": $ss_mtu
}
EOF
}

func_start_ss_dns(){
	dns=`echo -n $(awk '!/127.0.0.1/{print $2}' /etc/resolv.conf)| tr -s " " ","`
cat > "$dns_conf" <<EOF
bind-addr 127.0.0.1
bind-port 65353@udp
hosts /etc/hosts
no-ipv6 tag:gfw
EOF

if [ "$ss_mode" != "0" ]; then
cat >> "$dns_conf" <<EOF
china-dns $dns
trust-dns $ss_dns
gfwlist-file /etc/storage/gfwlist/gfwlist_domain.txt
gfwlist-file /etc/storage/chinadns/gfw_add.txt
EOF

if [ "$ss_mode" = "2" ]; then
cat >> "$dns_conf" <<EOF
default-tag chn
add-taggfw-ip ss_spec_dst_fw
EOF
else
cat >> "$dns_conf" <<EOF
chnlist-file /etc/storage/chinadns/chnlist.txt
chnlist-file /tmp/chnlist.txt
ipset-name4 chnroute
add-tagchn-ip ss_spec_dst_bp
# verdict 缓存 (用于 tag:none 域名)
verdict-cache 4096
EOF
fi

else
cat >> "$dns_conf" <<EOF
trust-dns $ss_dns
default-tag gfw
EOF

fi

cat >> "$dns_conf" <<EOF

# dns cache
cache 4096
cache-stale 86400
cache-refresh 20

EOF

cat > "/etc/dnsmasq/dnsmasq_ex.conf" <<EOF
no-resolv
no-poll
server=127.0.0.1#65353
EOF

	restart_dhcpd
	sh -c "chinadns-ng -C $dns_conf &"
}

func_stop_ss_dns(){
	killall -q chinadns-ng
	restart_dhcpd
}

func_stop(){
	func_stop_ss_dns
	killall -q $ss_bin
	ipset  destroy  chnroute
	ss-rules -f && loger $ss_bin "stop"
}

func_start(){
	ulimit -n 65536
	func_gen_ss_json && \
	func_start_ss_redir && \
	func_dl_list && \
	func_start_ss_rules && \
	func_start_ss_dns && \
	restart_firewall && \
	loger $ss_bin "start done" || { ss-rules -f && func_stop && loger $ss_bin "start fail!";}
}

case "$1" in
start)
	func_start
	;;
stop)
	func_stop
	;;
restart)
	func_stop
	func_start
	;;
*)
	echo "Usage: $0 { start | stop | restart }"
	exit 1
	;;
esac
