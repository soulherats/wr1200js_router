#!/bin/sh

ss_bin="ss-redir"
ss_json_file="/tmp/ss-redir.json"
dns_conf="/etc/china_dns.conf"
ss_proc="/var/ss-redir"
gfwlist="/etc/storage/gfwlist/gfwlist_domain.txt"
ss_dns="tcp://8.8.8.8,tcp://8.8.4.4"
ss_pid="0"

# UOTD (UDP over TCP DNS) variables
SS_LOCAL="/usr/bin/ss-local"
SS_UOT="/usr/bin/uote"
ss_uot_local_port="1053"
ss_uot_redir_port="1090"
ss_uot_log="/tmp/ss-uotd.log"
ss_lock_dir="/tmp/shadowsocks.lock"
ss_obfs_pid=""
ss_obfs_port=""

#/usr/bin/ss-redir -> /var/ss-redir -> /usr/bin/ss-orig-redir or /usr/bin/ssr-redir

ss_type="$(nvram get ss_type)" #0=ss;1=ssr
ss_simple_obfs="$(nvram get ss_simple_obfs)" #0=none;1=obfs_local

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
ss_uot=$(nvram get ss_uot) #0:off;1:UOTD DNS

loger() {
	logger -st "$1" "$2"
}

release_lock() {
	rm -f "$ss_lock_dir/pid"
	rmdir "$ss_lock_dir" 2>/dev/null
}

acquire_lock() {
	local lock_pid tries=0
	while ! mkdir "$ss_lock_dir" 2>/dev/null; do
		lock_pid=$(cat "$ss_lock_dir/pid" 2>/dev/null)
		if [ -z "$lock_pid" ]; then
			tries=$((tries + 1))
			[ "$tries" -ge 30 ] && return 1
			sleep 1
		elif ! kill -0 "$lock_pid" 2>/dev/null; then
			rm -f "$ss_lock_dir/pid"
			rmdir "$ss_lock_dir" 2>/dev/null
		else
			tries=$((tries + 1))
			[ "$tries" -ge 30 ] && return 1
			sleep 1
		fi
	done
	echo "$$" > "$ss_lock_dir/pid"
	trap release_lock EXIT
	trap 'release_lock; exit 1' INT TERM
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

get_obfs_pid(){
	local pid parent_pid
	for pid in $(pidof obfs-local); do
		parent_pid=$(awk '{print $4}' "/proc/$pid/stat" 2>/dev/null)
		[ "$parent_pid" = "$ss_pid" ] && {
			echo "$pid"
			return 0
		}
	done
	return 1
}

get_obfs_port(){
	# Match the listening socket owned by this ss-redir plugin instance.
	local obfs_pid="$1" inode hexport
	[ -z "$obfs_pid" ] && return 1
	for inode in $(ls -l /proc/$obfs_pid/fd 2>/dev/null | grep -o 'socket:\[[0-9]*\]' | grep -o '[0-9]*'); do
		hexport=$(awk -v i="$inode" '$10 == i && $4 == "0A" { print $2 }' /proc/net/tcp /proc/net/tcp6 2>/dev/null | head -1 | cut -d: -f2)
		[ -n "$hexport" ] && break
	done
	[ -z "$hexport" ] && return 1
	echo $((0x$hexport))
}

wait_obfs_port(){
	local tries=0
	while [ "$tries" -lt 10 ]; do
		kill -0 "$ss_pid" 2>/dev/null || return 1
		ss_obfs_pid=$(get_obfs_pid)
		[ -n "$ss_obfs_pid" ] && ss_obfs_port=$(get_obfs_port "$ss_obfs_pid")
		[ -n "$ss_obfs_port" ] && return 0
		tries=$((tries + 1))
		sleep 1
	done
	return 1
}

func_start_ss_redir(){
	: > /tmp/ss-redir.log
	if [ "${ss_type:-0}" = "0" ] && [ "${ss_simple_obfs:-0}" = "1" ]; then
		"$ss_bin" -c "$ss_json_file" $(get_arg_udp) --plugin obfs-local \
			--plugin-opts "$(nvram get ss_obfs_param)" >>/tmp/ss-redir.log 2>&1 &
	else
		"$ss_bin" -c "$ss_json_file" $(get_arg_udp) >>/tmp/ss-redir.log 2>&1 &
	fi
	ss_pid=$!
	sleep 1
	if ! kill -0 "$ss_pid" 2>/dev/null; then
		loger "$ss_bin" "process exited during startup, see /tmp/ss-redir.log"
		return 1
	fi
	if [ "${ss_type:-0}" = "0" ] && [ "${ss_simple_obfs:-0}" = "1" ]; then
		if ! wait_obfs_port; then
			loger "$ss_bin" "obfs-local did not become ready, see /tmp/ss-redir.log"
			return 1
		fi
	fi
	return 0
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
	if ! kill -0 "$ss_pid" 2>/dev/null; then
		return 1;
	fi
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

	#dns.goole.com
	for ip in 8.8.8.8 8.8.4.4; do
		ipset add ss_spec_dst_fw $ip -exist
	done
	restart_dhcpd
	sh -c "chinadns-ng -C $dns_conf" &
	return $?
}

func_stop_ss_dns(){
	killall -q chinadns-ng
	cat /dev/null > /etc/dnsmasq/dnsmasq_ex.conf
	restart_dhcpd
}

# === UOTD (UDP over TCP DNS) functions ===

func_gen_uot_json(){
	local uot_server="$ss_server"
	local uot_server_port="$ss_server_port"

        if [ "$ss_simple_obfs" = "1" ]; then
		uot_server_port="$ss_obfs_port"
		if [ -z "$uot_server_port" ]; then
			wait_obfs_port
			uot_server_port="$ss_obfs_port"
		fi
                if [ -z "$uot_server_port" ]; then
                        loger "ss-uotd" "obfs-local port not found, abort UOTD"
                        return 1
                fi
                uot_server="127.0.0.1"
        fi

	cat > "/tmp/ss-uot.json" <<EOF
{
    "server": "$uot_server",
    "server_port": $uot_server_port,
    "password": "$ss_password",
    "method": "$ss_method",
    "timeout": $ss_timeout,
    "protocol": "$ss_protocol",
    "protocol_param": "$ss_proto_param",
    "obfs": "$ss_obfs",
    "obfs_param": "$ss_obfs_param",
    "local_address": "0.0.0.0",
    "local_port": $ss_uot_redir_port,
    "mtu": $ss_mtu
}
EOF
}

func_start_ss_uotd(){
	[ "$ss_uot" != "1" ] && return 0
	if [ ! -x "$SS_LOCAL" ] || [ ! -x "$SS_UOT" ]; then
		loger "ss-uotd" "ss-local or uote is unavailable"
		return 1
	fi
	[ -n "$(pidof ss-local uote)" ] && func_stop_ss_uotd

	func_gen_uot_json || return 1

	: > "$ss_uot_log"
	"$SS_LOCAL" -c "/tmp/ss-uot.json" >>"$ss_uot_log" 2>&1 &
	local ss_local_pid
	ss_local_pid=$!
	sleep 1
	if ! kill -0 "$ss_local_pid" 2>/dev/null; then
		loger "ss-uotd" "ss-local failed to start, see $ss_uot_log"
		return 1
	fi

	"$SS_UOT" -l "$ss_uot_local_port" -p "$ss_uot_redir_port" >>"$ss_uot_log" 2>&1 &
	local uote_pid
	uote_pid=$!
	sleep 1
	if ! kill -0 "$uote_pid" 2>/dev/null; then
		loger "ss-uotd" "uote failed to start, see $ss_uot_log"
		func_stop_ss_uotd
		return 1
	fi

	iptables -t mangle -I SS_SPEC_WAN_FW 1 -p udp --dport 53 -j TPROXY --on-port $ss_uot_local_port --tproxy-mark 0x64
	sed -i "/\-A SS_SPEC_WAN_FW -p udp -j TPROXY/i -I SS_SPEC_WAN_FW 1 -p udp --dport 53 -j TPROXY --on-port $ss_uot_local_port --tproxy-mark 0x64" "/tmp/shadowsocks_iptables.save"
}

func_stop_ss_uotd(){
	while iptables -t mangle -D SS_SPEC_WAN_FW -p udp --dport 53 -j TPROXY --on-port "$ss_uot_local_port" --tproxy-mark 0x64 2>/dev/null; do
		:
	done
	if [ -f /tmp/shadowsocks_iptables.save ]; then
		sed -i "/TPROXY --on-port $ss_uot_local_port --tproxy-mark 0x64/d" /tmp/shadowsocks_iptables.save
	fi
	local sn="ss-local"
	local un="uote"

	local pids=$(pidof "$sn" "$un")
	[ -n "$pids" ] && kill $pids >/dev/null 2>&1

	local ex_pids=$(ps -w | grep -E "${sn}|${un}" | grep -v grep | awk '{print $1}')
	[ -n "$ex_pids" ] && kill $ex_pids >/dev/null 2>&1

	rm -f /tmp/ss-uot.json
}

func_stop(){
	func_stop_ss_uotd
	func_stop_ss_dns
	killall -q $ss_bin
	if [ "$ss_mode" = "1" ]; then
		ipset  destroy  chnroute
	fi
	ss-rules -f && loger $ss_bin "stop"
}

func_start(){
	if [ "$ss_uot" = "1" ] && [ "$ss_udp" != "1" ]; then
		nvram set ss_udp=1
		ss_udp=1
	fi
	ulimit -n 65536
	if func_gen_ss_json && \
		func_start_ss_redir && \
		func_dl_list && \
		func_start_ss_rules && \
		func_start_ss_dns; then
		if ! func_start_ss_uotd; then
			func_stop_ss_uotd
			loger "ss-uotd" "start failed; Shadowsocks continues without UOTD"
		fi
		loger $ss_bin "start done"
	else
		ss-rules -f
		func_stop
		loger $ss_bin "start fail!"
	fi
}

if ! acquire_lock; then
	loger "$ss_bin" "another start/stop operation is still running"
	exit 1
fi

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
