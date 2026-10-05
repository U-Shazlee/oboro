# Playing away from home: the relay

Oboro normally needs the 3DS and the PC on the same network. To play from
anywhere, the 3DS has to reach the PC over the internet, and many connections
(mobile broadband, a SIM in a router, some fibre providers) give the PC no
public address to reach.

A relay solves that. It is a small rented server with a public address:

```
3DS  -->  internet  -->  relay server  ==WireGuard tunnel==>  your PC
          (any Wi-Fi)    (public IP)      (opened by the PC)
```

The PC opens an encrypted tunnel out to the server and keeps it open. The
server passes Sunshine's and Oboro Host's ports down the tunnel. On the 3DS
you type the server's address instead of the PC's; nothing else changes.

**Cost:** one small server, about $6 a month. **Time:** about 30 minutes.

> **Not yet tested end to end.** Oboro Host's key passes its self-check and
> the 3DS code type-checks, but the scripts in `relay/` have not been run on
> a real server and the 3DS side has not been run on a console. Expect to fix
> something the first time.

## Before you start

- Do the whole setup **at the PC**. Pairing shows a PIN that has to be typed
  into Sunshine on the PC.
- You need this version of Oboro on the 3DS **and** this version of
  `host/oboro_host.py` on the PC. They changed together: Oboro Host now asks
  for a key, and older builds of Oboro do not send one.
- The PC must stay on, awake and signed in while you are away. Turn off
  sleep in Windows (Settings > System > Power).

## 1. Rent the server

These steps use [Vultr](https://www.vultr.com/) because it has two UK
locations. Any provider works if it gives you Ubuntu or Debian with a public
IPv4 address. The labels on Vultr's site may differ a little from these.

1. Create an account and choose **Deploy > Cloud Compute** (shared CPU).
2. Location: the one nearest your PC's internet connection. In the UK,
   create one server in **London** and one in **Manchester**, and compare
   them in step 2.
3. Image: **Ubuntu 24.04 LTS**.
4. Plan: the smallest plan **with an IPv4 address**, such as High
   Performance 1 vCPU / 1 GB. The cheapest plan is IPv6 only, and the 3DS
   cannot use IPv6. A bigger plan does not make the stream faster.
5. Turn **automatic backups off** (they cost extra and there is nothing to
   back up) and deploy.
6. Open the server's page and note its **IP address** and **root password**.

If you attach a Vultr *Firewall Group* to the server, it must allow UDP
51820, TCP 47984, 47989, 48010 and 48100, and UDP 47998-48000. With no group
attached, nothing needs doing.

## 2. Pick the faster location

On the PC, on its normal internet connection, open PowerShell and ping each
server:

```
ping -n 50 <server address>
```

Keep the server with the lower average **and** the steadier times; steady
matters as much as low. Destroy the other one on Vultr's site so it stops
costing money.

Under about 40 ms is good. If both are high and jumpy, the limit is the PC's
own connection, and no server location will fix that.

## 3. Set up the relay

From the Oboro folder on the PC, copy the script to the server and run it.
Windows has `scp` and `ssh` built in; both ask for the root password.

```
scp relay/setup-vps.sh root@<server address>:
ssh root@<server address> bash setup-vps.sh
```

At the end it prints the tunnel settings between two lines of dashes. Copy
that text into a new file on the PC named **`oboro-relay.conf`**. It holds
the PC's private key, so keep it out of shared folders.

Running the script again changes nothing and prints the settings again.

## 4. Connect the PC

1. Install [WireGuard for Windows](https://www.wireguard.com/install/).
2. Open WireGuard, choose **Import tunnel(s) from file**, pick
   `oboro-relay.conf`, and press **Activate**. WireGuard reconnects it by
   itself after a restart.
3. Open PowerShell **as administrator** in the Oboro folder and run:

   ```
   powershell -ExecutionPolicy Bypass -File relay\setup-pc.ps1
   ```

   It adds two firewall rules that let the relay (and only the relay) reach
   Sunshine and Oboro Host, then checks the tunnel and that both programs
   are running.
4. Restart Oboro Host so the new version is the one running, and read its
   key:

   ```
   python host/oboro_host.py --key
   ```

   The key is also shown at `http://localhost:48100`.

## 5. Point the 3DS at the relay

1. Install this version of Oboro on the 3DS.
2. If the 3DS is already paired with the PC's local address, open
   **Settings > Your PC > Paired PC** and forget it.
3. Press **A** and type the **relay's IP address**.
4. Type the **12-digit key** from step 4.
5. Oboro shows a PIN. On the PC, open `https://localhost:47990`, go to
   **PIN**, and enter it.
6. Open **Settings > Network > Connection type** and choose
   **Weak / hotspot** to begin with. Try Standard once it plays well.

The relay's address works everywhere, including at home on the PC's own
Wi-Fi, though at home the picture then takes the long way round. To go back
to the direct connection, forget the PC and pair again with its local
address.

## 6. Try it

Connect the 3DS to a different network, such as a phone's hotspot, and start
a game. The ping tile on the Stream stats page shows the delay you are
getting.

## If it doesn't work

| What you see | What to check |
|---|---|
| "Can't reach ..." on the 3DS | Is the tunnel active in WireGuard on the PC? Run `relay\setup-pc.ps1` again: it says what is missing. On the server, `wg show` should list a recent "latest handshake". |
| Sunshine's apps appear instead of your library, with "Oboro Host needs its key" | Press **Y** in the library; Oboro asks for the key again. Compare it with `python host/oboro_host.py --key`. |
| Pairing never finishes | The PIN must be entered on the PC within two minutes. Press **Y** for a new one. |
| The library loads but "No video arrived" | UDP 47998-48000 is blocked. Run `relay\setup-pc.ps1` again, and check any Vultr Firewall Group. |
| Picture breaks up or stutters | Use Weak / hotspot and a lower bitrate. Check the PC's upload speed and the ping in step 2. |
| It worked, then stopped after the PC restarted | Oboro Host starts when you sign in to Windows (`--install`); the PC has to be signed in, not at the lock screen. |

## What this exposes

- Sunshine's streaming ports are reachable from the internet at the relay's
  address. Sunshine only talks to devices that were paired with a PIN.
- Sunshine's settings page (port 47990) is **not** passed on. Pairing a new
  device still needs someone at the PC.
- Oboro Host is reachable too, which is why it now needs the key. It is
  sent unencrypted between the 3DS and the relay, so someone watching the
  Wi-Fi you play on could read it. With it they could see your library and
  PC stats and start a game that is already in your library; they could not
  see the stream or control the PC. To change the key, delete
  `%APPDATA%\Oboro\key.txt`, restart Oboro Host, and type the new key on
  the 3DS.
- The server's root password is the weakest point of the server itself. Use
  a long one, or set up SSH keys.

## Removing it

Destroy the server on the provider's site, remove the tunnel in WireGuard,
and delete the two firewall rules:

```
Get-NetFirewallRule -DisplayName 'Oboro relay*' | Remove-NetFirewallRule
```
