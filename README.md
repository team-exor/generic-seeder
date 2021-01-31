# generic-seeder

### v1.1.0

The generic-seeder is a blockchain network crawler that maintains a list of IP addresses of the most reliable nodes on the network and shares those node IPs via DNS request to anyone requiring an entry point into the decentralized network. Choose between two main usage modes which consist of locally running a lightweight DNS server or feeding the data into a [Cloudflare](https://www.cloudflare.com/) account in order to respond to DNS seed requests. If you just want to crawl a network to get a list of the connectable nodes, without worrying about the DNS setup, you can do that too. The seeder app is compatible with almost any bitcoin-based blockchain network and can be configured in a short amount of time by filling out a small handful of parameters in the configuration file with the data from your coin's network. Tested to work with Ubuntu 18.04+ and Debian 8.x+ but it should work fine on any Linux installation, although package names and install steps may differ.

## Table of Contents

- [Features](#features)
- [Full Setup Instructions](#full-setup-instructions)
- [Quick Setup](#quick-setup)
  - [Step 1 - Install Prerequisites](#step-1---install-prerequisites)
  - [Step 2 - Download Source Code](#step-2---download-source-code)
  - [Step 3 - Navigate to Source Directory](#step-3---navigate-to-source-directory)
  - [Step 4 - Configure Seeder Settings](#step-4---configure-seeder-settings)
  - [Step 5 - Build from Source](#step-5---build-from-source)
- [Usage](#usage)
  - [Local DNS Server Mode](#local-dns-server-mode)
    - [Non-Root Workarounds](#non-root-workarounds)
  - [Cloudflare API Mode](#cloudflare-api-mode)
- [Command-Line Options](#command-line-options)

## Features

- Regularly revisits known nodes to check their availability
- [Cloudflare](https://www.cloudflare.com/) DNS integration
- Block explorer integration (including support for a 2nd failover explorer)
- Keep a running list of all nodes in the network or only show nodes that are above a certain version
- Bans/Unlists nodes after enough failures, or bad behaviour
- Keeps statistics over (exponential) windows of 2 hours, 8 hours, 1 day and 1 week, to base decisions on
- Very low memory (a few tens of megabytes) and cpu requirements
- Run multiple crawlers at the same time (96 threads simultaneously by default)
- Force connections to IPv4 or IPv6 only if desired
- Customizable options via configuration file

## Full Setup Instructions

Detailed set up instructions can be found in the [DNS Seeder Setup Guide](/SETUP.md)

## Quick Setup

### Step 1 - Install Prerequisites

```
sudo apt-get install git build-essential libboost-all-dev libssl-dev libcurl4-openssl-dev libconfig++-dev
```

### Step 2 - Download Source Code

```
git clone https://github.com/team-exor/generic-seeder.git
```

### Step 3 - Navigate to Source Directory

```
cd generic-seeder
```

### Step 4 - Configure Seeder Settings

```
cp ./settings.conf.template ./settings.conf
```

*Make required changes in settings.conf*

### Step 5 - Build from Source

```
make
```

*This will produce the `dnsseed` binary*

## Usage

### Local DNS Server Mode

The seeder app comes with a built-in DNS server which listens for DNS requests and serves results based on the IP addresses that have been crawled on your blockchain network. If, for example, you want to run a DNS seed on dnsseed.example.com, you will need an authorative NS record in example.com's domain record that points back to your server. It is required to create both an NS and an "A" record to tie everything together.

**Example:**

| Record Type | Name/Host | Value/Nameserver |
| ----------- | --------- | ---------------- |
| A           | vps       | 123.231.123.231  |
| NS          | dnsseed   | vps.example.com  |

You can now run the seeder app on the vps.example.com system using the following terminal cmd (must be run with root permissions or the DNS server will not be able to listen for and respond to requests properly):

```
sudo ./dnsseed -h dnsseed.example.com -n vps.example.com
```

If you want the DNS server to report SOA records, you must provide an email address using the `-m` argument:

```
./dnsseed -h dnsseed.example.com -n vps.example.com -m email@example.com
```

#### Non-Root Workarounds

Because non-root users cannot access ports below 1024, an extra step is required to allow you to run the DNS server (which must always use port 53) without root privileges. There are two known options for running the seeder app using a non-root user account:

1. The first non-root method is to use the `setcap` command to change the capabilities of the `dnsseed` binary file to specifically allow the app to bind to a port less than 1024 (this one-time cmd requires root privileges):

```
sudo setcap 'cap_net_bind_service=+ep' /path/to/dnsseed
```

Once the `setcap` command is complete, you can start the seeder app as per normal, without the need for `sudo`:

```
./dnsseed -h dnsseed.example.com -n vps.example.com -m email@example.com
```

2. The second non-root method is to add a redirect entry for port 53 in the iptables firewall system before running the seeder app as a non-root user (this one-time cmd requires root privileges):

```
sudo iptables -t nat -A PREROUTING -p udp --dport 53 -j REDIRECT --to-port 5353
``` 

After adding the new iptables rule, the seeder app can be called without `sudo`, but you must always specify the redirected port using the `-p` argument:

```
./dnsseed -h dnsseed.example.com -n vps.example.com -m email@example.com -p 5353
```

### Cloudflare API Mode

Instead of using the DNS seeder app to run your own DNS server, you can alternatively utilize a free [Cloudflare](https://www.cloudflare.com/) account to host the list of good nodes from your blockchain network. Extra setup is required before Cloudflare mode will work properly. Python 3+ and the Cloudflare Python API must be installed. Run the following cmds in the terminal, one line at a time:

```
sudo apt-get install python3 python3-pip
sudo pip3 install cloudflare
```

You must also fill in the Cloudflare API config section at the bottom of the `settings.conf` file.

**Example:**

```
cf_domain="example.com"
cf_domain_prefix="dnsseed"
cf_username="your_cloudflare_username"
cf_api_key="your_cloudflare_api_key"
cf_seed_dump="dnsseed.dump"
```

Run the seeder without the need to specify any additional options:

```
./dnsseed
```

Let the seeder app run for a few minutes until a `dnsseed.dump` file is generated, and then you can test Cloudflare mode:

```
cd /path/to/seeder/cf-uploader && python3 seeder.py
```

Assuming no errors were reported, you can check that your seeder domain is working properly by running the following cmd in the format `nslookup {cf_domain_prefix}.{cf_domain}`:

```
nslookup dnsseed.example.com
```

If everything is working correctly, you will see a number of "Name:" and "Address:" lines near the end of the output:

**Example:**

```
Server:         2001:19f1:300:1702::3
Address:        2001:19f1:300:1702::3#53

Non-authoritative answer:
Name:   dnsseed.example.com
Address: 158.203.13.138
Name:   dnsseed.example.com
Address: 44.76.38.113
Name:   dnsseed.example.com
Address: 46.76.253.117
Name:   dnsseed.example.com
Address: 145.248.52.149
Name:   dnsseed.example.com
Address: 82.240.23.104
Name:   dnsseed.example.com
Address: 103.207.140.36
Name:   dnsseed.example.com
Address: 204.222.30.68
```

Once configured correctly, it is recommended to set up a cron job that will automatically update the seeds list every 30 minutes or so:

```
*/30 * * * * cd /path/to/seeder/cf-uploader && python3 seeder.py
```

## Command-Line Options

- `-h` or `--host`

     Hostname of the DNS seed

     Usage Example: `./dnsseed -h dnsseed.example.com`
- `-n` or `--ns`

     Hostname of the nameserver

     Usage Example: `./dnsseed -n vps.example.com`
- `-m` or `--mbox`

     E-Mail address reported in SOA records

     Usage Example: `./dnsseed -m email@example.com`
- `-t` or `--threads`

     Number of crawlers to run in parallel (default 96)

     Usage Example: `./dnsseed -t 150`
- `-d` or `--dnsthreads`

     Number of DNS server threads (default 4)

     Usage Example: `./dnsseed -d 10`
- `-a` or `--address`

     Address to listen on (default ::)

     Usage Example: `./dnsseed -a 24.45.22.148`
- `-p` or `--port`

     UDP port to listen on (default 53)

     Usage Example: `./dnsseed -p 5353`
- `-o` or `--onion`

     Tor proxy IP/Port

     Usage Example: `./dnsseed -o 127.0.0.1:9150`
- `-i` or `--proxyipv4`

     IPV4 SOCKS5 proxy IP/Port

     Usage Example: `./dnsseed -i 46.5.252.55:1080`
- `-k` or `--proxyipv6`

     IPV6 SOCKS5 proxy IP/Port

     Usage Example: `./dnsseed -k [2620:0:6b0:a:250:56ff:fe99:78f7]:1234`
- `-w` or `--filter`

     Allow these flag combinations as filters

     Usage Example: `./dnsseed -w 0x1,0x5,0x9,0xd,0x49,0x400`
- `-f` or `--forceip`

     Force connections to nodes of a specific IP type<br />valid options: a = all, 4 = IPv4, 6 = IPv6 (default a)

     Usage Example: `./dnsseed -f 4`
- `--wipeban`

     Wipe list of banned nodes

     Usage Example: `./dnsseed --wipeban`
- `--wipeignore`

     Wipe list of ignored nodes

     Usage Example: `./dnsseed --wipeignore`
- `--dumpall`

     Dump all unique nodes

     Usage Example: `./dnsseed --dumpall`
- `-?` or `--help`

     Show the help text

     Usage Example: `./dnsseed -?`

---
Need more help? Read the [DNS Seeder Setup Guide](/SETUP.md)