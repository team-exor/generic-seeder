generic-seeder
===============

The generic-seeder app is a crawler that can easily be customized for almost any blockchain network. It generates a list of nodes on the network and the most reliable nodes can be exposed via a built-in DNS server or pushed to a remote CloudFlare server.

Features:
- Regularly revisits known nodes to check their availability
- CloudFlare DNS integration
- Block explorer integration (including support for a 2nd failover explorer)
- Keep a running list of all nodes in the network or only show nodes that are above a certain version
- Bans/Unlists nodes after enough failures, or bad behaviour
- Keeps statistics over (exponential) windows of 2 hours, 8 hours, 1 day and 1 week, to base decisions on
- Very low memory (a few tens of megabytes) and cpu requirements
- Run multiple crawlers at the same time (96 threads simultaneously by default)
- Force connections to IPv4 or IPv6 only if desired
- Customizable options via configuration file

FULL SETUP INSTRUCTIONS
-----------------------
Detailed set up instructions can be found in the [DNS Seeder Setup Guide](/SETUP.md)

REQUIREMENTS
------------

```
sudo apt-get install build-essential libboost-all-dev libssl-dev libcurl4-openssl-dev libconfig++-dev
```

CONFIGURE
---------

```
cp ./settings.conf.template ./settings.conf
```

*Make required changes in settings.conf*

COMPILING
---------
Compiling will require boost and ssl.  On debian systems, these are provided
by `libboost-dev` and `libssl-dev` respectively.

```
make
```

This will produce the `dnsseed` binary.

USAGE: LOCAL DNS SERVER MODE
----------------------------

For example, if you want to run a DNS seed on dnsseed.example.com, you will need an authorative NS record in example.com's domain record that points back to your server. It is required to create both an NS and "A" record to tie everything together.

| Record Type | Name/Host | Value/Nameserver |
| ----------- | --------- | ---------------- |
| A           | vps       | 123.231.123.231  |
| NS          | dnsseed   | vps.example.com  |

On the system vps.example.com, you can now run the seeder app as the root user:

```
sudo ./dnsseed -h dnsseed.example.com -n vps.example.com
```

If you want the DNS server to report SOA records, you must provide an email address using the `-m` argument:

```
./dnsseed -h dnsseed.example.com -n vps.example.com -m email@example.com
```

There are two known options for running the seeder app using a non-root user:

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

USAGE: CLOUDFLARE API MODE
--------------------------

Extra setup is required before CloudFlare mode will work properly. Python 3+ and the Cloudflare Python API must be installed. Run the following cmds in the terminal, one line at a time:

```
sudo apt-get install python3 python3-pip
sudo pip3 install cloudflare
```

You must also fill in the CloudFlare API config section at the bottom
of the settings.conf file.

Example:

```
cf_domain="example.com"
cf_domain_prefix="dnsseed"
cf_username="your_cloudflare_username"
cf_api_key="your_cloudflare_api_key"
cf_seed_dump="dnsseed.dump"
```

Have the seeder above running all the time, but with no NS record:

```
./dnsseed
```

After a few minutes of running the generic seeder and the dnsseed.dump
file has been populated you can then test CloudFlare mode:

```
cd /path/to/seeder/cf-uploader && python3 seeder.py
```

Assuming no errors were reported, you can check that your seeder domain is
working properly by running the following cmd in the format
`nslookup {cf_domain_prefix}.{cf_domain}`:

```
nslookup dnsseed.example.com
```

If everything is working correctly, you will see a number of "Name:" and
"Address:" lines near the end of the output:

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

Once configured correctly, it is recommended to set up a cron job that will
automatically update the seeds list every 30 minutes or so:

```
*/30 * * * * cd /path/to/seeder/cf-uploader && python3 seeder.py
```

---
Need more help? Read the [DNS Seeder Setup Guide](/SETUP.md)
