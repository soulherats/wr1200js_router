#!/bin/sh

ss_bin=ss-local
ss_json_file="/tmp/ss-local.json"
gfwlist="/etc/storage/gfwlist/gfwlist_domain.txt"
ss_proc="/var/ss-tunnel"
#/usr/bin/ss-local -> /var/ss-tunnel -> /usr/bin/ss-orig-tunnel or /usr/bin/ssr-local

ss_type="$(nvram get ss_type)" #0=ss;1=ssr

if [ "${ss_type:-0}" = "0" ]; then
	ln -sf /usr/bin/ss-orig-tunnel $ss_proc
elif [ "${ss_type:-0}" = "1" ]; then
	ss_protocol=$(nvram get ss_protocol)
	ss_proto_param=$(nvram get ss_proto_param)
	ss_obfs=$(nvram get ss_obfs)
	ss_obfs_param=$(nvram get ss_obfs_param)
	ln -sf /usr/bin/ssr-local $ss_proc
fi

ss_server=$(nvram get ss_server)
ss_server_port=$(nvram get ss_server_port)
ss_method=$(nvram get ss_method)
ss_password=$(nvram get ss_key)

ss_tunnel_remote=$(nvram get ss-tunnel_remote)
ss_tunnel_mtu=$(nvram get ss-tunnel_mtu)
ss_tunnel_local_port=$(nvram get ss-tunnel_local_port)

ss_timeout=$(nvram get ss_timeout)

log_file="/tmp/ss-watchcat.log"
max_log_bytes=100000

loger(){
	[ -f $log_file ] && [ $(stat -c %s $log_file) -gt $max_log_bytes ] && rm -f $log_file
	time=$(date "+%H:%M:%S")
	echo "$time ss-tunnel $1" >> $log_file
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
    "local_port": $ss_tunnel_local_port,
    "mtu": $ss_tunnel_mtu
}

EOF
}

func_start_ss_tunnel(){
	func_gen_ss_json

	dns=` awk '{print $2}' /etc/resolv.conf| head -n 1`
	[ "$dns" == "8.8.8.8" ] && { sed -i '/Chinadns/,+4d' /etc/storage/dnsmasq/dnsmasq.conf; return 1; } 
	if [ $(awk '{print $2}' /etc/resolv.conf | wc -l) -gt 1 ]; then
		dns=`awk '{print $2}' /etc/resolv.conf | head -n2 | sed '1s/$/,/' | tr -d "\n"`
	fi
	if [ -z `grep Chinadns /etc/storage/dnsmasq/dnsmasq.conf` ];  then
		cat >> /etc/storage/dnsmasq/dnsmasq.conf <<EOF
### Chinadns
no-resolv
no-poll
server=127.0.0.1#65353

EOF
		restart_dhcpd
	fi
	sh -c "$ss_bin -u -c $ss_json_file -L $ss_tunnel_remote &" && sh -c "chinadns-ng -g $gfwlist -d chn -t 127.0.0.1#${ss_tunnel_local_port},2001:da8::666 -c $dns --no-ipv6=gt &"
	return $?
}

func_stop(){
	killall -q $ss_bin
	sed -i '/Chinadns/,+4d' /etc/storage/dnsmasq/dnsmasq.conf
	restart_dns
	killall -q chinadns-ng
}

case "$1" in
start)
	func_start_ss_tunnel && loger $ss_bin "ss_tunnel start done" || loger $ss_bin "dns start failed"
	;;
stop)
	func_stop
	;;
restart)
	func_stop
	func_start_ss_tunnel
	;;
*)
	echo "Usage: $0 { start | stop | restart }"
	exit 1
	;;
esac
