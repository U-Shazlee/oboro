#!/usr/bin/env bash
# Turn a small Linux server with a public IP address into Oboro's relay, for
# playing away from home when the PC has no public address of its own (mobile
# broadband, carrier-grade NAT). See docs/remote-play.md.
#
# The PC keeps a WireGuard tunnel open to this server. The server passes
# Sunshine's and Oboro Host's ports down that tunnel, so the 3DS streams from
# this server's address as if it were the PC.
#
# Run once as root on a fresh Ubuntu or Debian server:
#     bash setup-vps.sh
# It prints the tunnel settings for the PC at the end. Running it again
# changes nothing and prints them again.
set -euo pipefail

[ "$(id -u)" = 0 ] || { echo "Run this as root (sudo bash setup-vps.sh)."; exit 1; }

WG_DIR=/etc/wireguard
WG_PORT=51820
SERVER_IP=10.66.66.1
PC_IP=10.66.66.2
# Sunshine: HTTPS, HTTP and RTSP over TCP; video, control and audio over UDP.
# 48100 is Oboro Host. Sunshine's settings page (47990) is deliberately not
# passed on: it stays reachable from the PC itself only.
TCP_PORTS=47984,47989,48010,48100
UDP_PORTS=47998:48000

WAN_IF="$(ip -4 route show default | awk '{print $5; exit}')"
PUBLIC_IP="$(ip -4 route get 1.1.1.1 | awk '{for (i = 1; i < NF; i++) if ($i == "src") {print $(i + 1); exit}}')"
[ -n "$WAN_IF" ] && [ -n "$PUBLIC_IP" ] || { echo "Couldn't find this server's network interface."; exit 1; }

if ! command -v wg >/dev/null || ! command -v iptables >/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq wireguard iptables
fi

umask 077
mkdir -p "$WG_DIR"
cd "$WG_DIR"

if [ ! -f wg0.conf ]; then
    wg genkey | tee server.key | wg pubkey > server.pub
    wg genkey | tee pc.key | wg pubkey > pc.pub

    # The firewall rules, added when the tunnel starts and removed when it stops.
    cat > oboro-forward.sh <<EOF
#!/bin/sh
# Written by Oboro's setup-vps.sh. \$1: up or down.
[ "\$1" = up ] && A=-A I=-I || A=-D I=-D
iptables \$I INPUT -p udp --dport $WG_PORT -j ACCEPT
iptables \$I INPUT -i wg0 -p icmp -j ACCEPT
iptables -t nat \$A PREROUTING -i $WAN_IF -p tcp -m multiport --dports $TCP_PORTS -j DNAT --to-destination $PC_IP
iptables -t nat \$A PREROUTING -i $WAN_IF -p udp -m multiport --dports $UDP_PORTS -j DNAT --to-destination $PC_IP
iptables \$I FORWARD -i $WAN_IF -o wg0 -d $PC_IP -j ACCEPT
iptables \$I FORWARD -i wg0 -o $WAN_IF -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
# The PC sees every player as this server, so its answers come back here.
iptables -t nat \$A POSTROUTING -o wg0 -j MASQUERADE
EOF
    chmod 700 oboro-forward.sh

    # MTU 1280 fits inside mobile networks, whose own limit is below 1500.
    cat > wg0.conf <<EOF
[Interface]
Address = $SERVER_IP/24
ListenPort = $WG_PORT
PrivateKey = $(cat server.key)
MTU = 1280
PostUp = $WG_DIR/oboro-forward.sh up
PostDown = $WG_DIR/oboro-forward.sh down

[Peer]
# The gaming PC
PublicKey = $(cat pc.pub)
AllowedIPs = $PC_IP/32
EOF

    cat > oboro-relay.conf <<EOF
[Interface]
PrivateKey = $(cat pc.key)
Address = $PC_IP/24
MTU = 1280

[Peer]
PublicKey = $(cat server.pub)
Endpoint = $PUBLIC_IP:$WG_PORT
AllowedIPs = $SERVER_IP/32
PersistentKeepalive = 25
EOF

    echo "net.ipv4.ip_forward = 1" > /etc/sysctl.d/99-oboro-relay.conf
    sysctl -q -p /etc/sysctl.d/99-oboro-relay.conf
    systemctl enable -q --now wg-quick@wg0
fi

systemctl is-active -q wg-quick@wg0 || { echo "The tunnel did not start: journalctl -u wg-quick@wg0"; exit 1; }

cat <<EOF

The relay is running. Its address, to type on the 3DS:

    $PUBLIC_IP

Save everything between the lines below on the PC as a file named
oboro-relay.conf, then import it in WireGuard. It holds the PC's private key:
do not share it.
------------------------------------------------------------------------
$(cat "$WG_DIR/oboro-relay.conf")
------------------------------------------------------------------------
Once the PC is connected, "wg show" here lists a "latest handshake".
EOF
