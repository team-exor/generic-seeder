#include <algorithm>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <atomic>
#include <iostream>
#include <string>
#include <curl/curl.h>
#include <libconfig.h++>
#include "bitcoin.h"
#include "db.h"

using namespace std;
using namespace libconfig;

bool fDumpAll = false;
bool bCurrentBlockFromExplorer = false;
string sForceIP;
string sCurrentBlock;
int nCurrentBlock = -1;
int nDefaultBlockHeight = -1;

int cfg_protocol_version;
int cfg_init_proto_version;
int cfg_min_peer_proto_version;
int cfg_caddr_time_version;
unsigned char cfg_message_start[4];
int cfg_wallet_port;
string cfg_explorer_url;
string cfg_explorer_url2;
int cfg_explorer_requery_seconds;

class CDnsSeedOpts {
public:
  int nThreads;
  int nPort;
  int nDnsThreads;
  int fWipeBan;
  int fWipeIgnore;
  int fDumpAll;
  const char *mbox;
  const char *ns;
  const char *host;
  const char *tor;
  const char *ipv4_proxy;
  const char *ipv6_proxy;
  const char *force_ip;
  std::set<uint64_t> filter_whitelist;

  CDnsSeedOpts() : nThreads(96), nDnsThreads(4), nPort(53), mbox(NULL), ns(NULL), host(NULL), tor(NULL), fWipeBan(false), fWipeIgnore(false), fDumpAll(false), ipv4_proxy(NULL), ipv6_proxy(NULL), force_ip("a") {}

  void ParseCommandLine(int argc, char **argv) {
    static const char *help = "generic-seeder\n"
                              "Usage: %s -h <host> -n <ns> [-m <mbox>] [-t <threads>] [-p <port>]\n"
                              "\n"
                              "Options:\n"
                              "-h <host>       Hostname of the DNS seed\n"
                              "-n <ns>         Hostname of the nameserver\n"
                              "-m <mbox>       E-Mail address reported in SOA records\n"
                              "-t <threads>    Number of crawlers to run in parallel (default 96)\n"
                              "-d <threads>    Number of DNS server threads (default 4)\n"
                              "-p <port>       UDP port to listen on (default 53)\n"
                              "-o <ip:port>    Tor proxy IP/Port\n"
                              "-i <ip:port>    IPV4 SOCKS5 proxy IP/Port\n"
                              "-k <ip:port>    IPV6 SOCKS5 proxy IP/Port\n"
                              "-w f1,f2,...    Allow these flag combinations as filters\n"
                              "-f <ip version> Force connections to nodes of a specific ip type\n"
                              "                valid options: a = all, 4 = IPv4, 6 = IPv6 (default a)\n"
                              "--wipeban       Wipe list of banned nodes\n"
                              "--wipeignore    Wipe list of ignored nodes\n"
                              "--dumpall       Dump all unique nodes\n"
                              "-?, --help      Show this text\n"
                              "\n";
    bool showHelp = false;

    while(1) {
      static struct option long_options[] = {
        {"host", required_argument, 0, 'h'},
        {"ns",   required_argument, 0, 'n'},
        {"mbox", required_argument, 0, 'm'},
        {"threads", required_argument, 0, 't'},
        {"dnsthreads", required_argument, 0, 'd'},
        {"port", required_argument, 0, 'p'},
        {"onion", required_argument, 0, 'o'},
        {"proxyipv4", required_argument, 0, 'i'},
        {"proxyipv6", required_argument, 0, 'k'},
        {"filter", required_argument, 0, 'w'},
        {"forceip", required_argument, 0, 'f'},
        {"wipeban", no_argument, &fWipeBan, 1},
        {"wipeignore", no_argument, &fWipeIgnore, 1},
        {"dumpall", no_argument, &fDumpAll, 1},
        {"help", no_argument, 0, '?'},
        {0, 0, 0, 0}
      };
      int option_index = 0;
      int c = getopt_long(argc, argv, "h:n:m:t:p:d:o:i:k:w:f:?", long_options, &option_index);
      if (c == -1) break;
      switch (c) {
        case 'h': {
          host = optarg;
          break;
        }
        
        case 'm': {
          mbox = optarg;
          break;
        }
        
        case 'n': {
          ns = optarg;
          break;
        }
        
        case 't': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n < 1000) nThreads = n;
          break;
        }

        case 'd': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n < 1000) nDnsThreads = n;
          break;
        }

        case 'p': {
          int p = strtol(optarg, NULL, 10);
          if (p > 0 && p < 65536) nPort = p;
          break;
        }

        case 'o': {
          tor = optarg;
          break;
        }

        case 'i': {
          ipv4_proxy = optarg;
          break;
        }

        case 'k': {
          ipv6_proxy = optarg;
          break;
        }

        case 'w': {
          char* ptr = optarg;
          while (*ptr != 0) {
            unsigned long l = strtoul(ptr, &ptr, 0);
            if (*ptr == ',') {
                ptr++;
            } else if (*ptr != 0) {
                break;
            }
            filter_whitelist.insert(l);
          }
          break;
        }

        case 'f': {
          force_ip = optarg;
          break;
        }

        case '?': {
          showHelp = true;
          break;
        }
      }
    }
    if (filter_whitelist.empty()) {
        filter_whitelist.insert(NODE_NETWORK); // x1
        filter_whitelist.insert(NODE_NETWORK | NODE_BLOOM); // x5
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS); // x9
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_COMPACT_FILTERS); // x49
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_BLOOM); // xd
        filter_whitelist.insert(NODE_NETWORK_LIMITED); // x400
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_BLOOM); // x404
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS); // x408
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_COMPACT_FILTERS); // x448
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_BLOOM); // x40c
    }
    if (host != NULL && ns == NULL) showHelp = true;
    if (showHelp) {
        fprintf(stderr, help, argv[0]);
        exit(0);
    }
  }
};

#include "dns.h"

CAddrDb db;

extern "C" void* ThreadCrawler(void* data) {
  int *nThreads=(int*)data;
  do {
    std::vector<CServiceResult> ips;
    int wait = 5;
    db.GetMany(ips, 16, wait);
    int64 now = time(NULL);
    if (ips.empty()) {
      wait *= 1000;
      wait += rand() % (500 * *nThreads);
      Sleep(wait);
      continue;
    }
    vector<CAddress> addr;
    for (int i=0; i<ips.size(); i++) {
      CServiceResult &res = ips[i];
      res.nBanTime = 0;
      res.nClientV = 0;
      res.nHeight = 0;
      res.strClientV = "";
	  res.bInSync = false;
      bool getaddr = res.ourLastSuccess + 86400 < now;
      res.fGood = TestNode(res.service,res.nBanTime,res.nClientV,res.strClientV,res.nHeight,res.bInSync,getaddr ? &addr : NULL);
    }
    db.ResultMany(ips);
    db.Add(addr);
  } while(1);
  return nullptr;
}

extern "C" int GetIPList(void *thread, char *requestedHostname, addr_t *addr, int max, int ipv4, int ipv6);

class CDnsThread {
public:
  struct FlagSpecificData {
      int nIPv4, nIPv6;
      std::vector<addr_t> cache;
      time_t cacheTime;
      unsigned int cacheHits;
      FlagSpecificData() : nIPv4(0), nIPv6(0), cacheTime(0), cacheHits(0) {}
  };

  dns_opt_t dns_opt; // must be first
  const int id;
  std::map<uint64_t, FlagSpecificData> perflag;
  std::atomic<uint64_t> dbQueries;
  std::set<uint64_t> filterWhitelist;

  void cacheHit(uint64_t requestedFlags, bool force = false) {
    static bool nets[NET_MAX] = {};
    if (!nets[NET_IPV4]) {
        nets[NET_IPV4] = true;
        nets[NET_IPV6] = true;
    }
    time_t now = time(NULL);
    FlagSpecificData& thisflag = perflag[requestedFlags];
    thisflag.cacheHits++;
    if (force || thisflag.cacheHits * 400 > (thisflag.cache.size()*thisflag.cache.size()) || (thisflag.cacheHits*thisflag.cacheHits * 20 > thisflag.cache.size() && (now - thisflag.cacheTime > 5))) {
      set<CNetAddr> ips;
      db.GetIPs(ips, requestedFlags, 1000, nets);
      dbQueries++;
      thisflag.cache.clear();
      thisflag.nIPv4 = 0;
      thisflag.nIPv6 = 0;
      thisflag.cache.reserve(ips.size());
      for (set<CNetAddr>::iterator it = ips.begin(); it != ips.end(); it++) {
        struct in_addr addr;
        struct in6_addr addr6;
        if ((*it).GetInAddr(&addr)) {
          addr_t a;
          a.v = 4;
          memcpy(&a.data.v4, &addr, 4);
          thisflag.cache.push_back(a);
          thisflag.nIPv4++;
        } else if ((*it).GetIn6Addr(&addr6)) {
          addr_t a;
          a.v = 6;
          memcpy(&a.data.v6, &addr6, 16);
          thisflag.cache.push_back(a);
          thisflag.nIPv6++;
        }
      }
      thisflag.cacheHits = 0;
      thisflag.cacheTime = now;
    }
  }

  CDnsThread(CDnsSeedOpts* opts, int idIn) : id(idIn) {
    dns_opt.host = opts->host;
    dns_opt.ns = opts->ns;
    dns_opt.mbox = opts->mbox;
    dns_opt.datattl = 3600;
    dns_opt.nsttl = 40000;
    dns_opt.cb = GetIPList;
    dns_opt.port = opts->nPort;
    dns_opt.nRequests = 0;
    dbQueries = 0;
    perflag.clear();
    filterWhitelist = opts->filter_whitelist;
  }

  void run() {
    dnsserver(&dns_opt);
  }
};

extern "C" int GetIPList(void *data, char *requestedHostname, addr_t* addr, int max, int ipv4, int ipv6) {
  CDnsThread *thread = (CDnsThread*)data;

  uint64_t requestedFlags = 0;
  int hostlen = strlen(requestedHostname);
  if (hostlen > 1 && requestedHostname[0] == 'x' && requestedHostname[1] != '0') {
    char *pEnd;
    uint64_t flags = (uint64_t)strtoull(requestedHostname+1, &pEnd, 16);
    if (*pEnd == '.' && pEnd <= requestedHostname+17 && std::find(thread->filterWhitelist.begin(), thread->filterWhitelist.end(), flags) != thread->filterWhitelist.end())
      requestedFlags = flags;
    else
      return 0;
  }
  else if (strcasecmp(requestedHostname, thread->dns_opt.host))
    return 0;
  thread->cacheHit(requestedFlags);
  auto& thisflag = thread->perflag[requestedFlags];
  unsigned int size = thisflag.cache.size();
  unsigned int maxmax = (ipv4 ? thisflag.nIPv4 : 0) + (ipv6 ? thisflag.nIPv6 : 0);
  if (max > size)
    max = size;
  if (max > maxmax)
    max = maxmax;
  int i=0;
  while (i<max) {
    int j = i + (rand() % (size - i));
    do {
        bool ok = (ipv4 && thisflag.cache[j].v == 4) ||
                  (ipv6 && thisflag.cache[j].v == 6);
        if (ok) break;
        j++;
        if (j==size)
            j=i;
    } while(1);
    addr[i] = thisflag.cache[j];
    thisflag.cache[j] = thisflag.cache[i];
    thisflag.cache[i] = addr[i];
    i++;
  }
  return max;
}

vector<CDnsThread*> dnsThread;

extern "C" void* ThreadDNS(void* arg) {
  CDnsThread *thread = (CDnsThread*)arg;
  thread->run();
  return nullptr;
}

int StatCompare(const CAddrReport& a, const CAddrReport& b) {
  if (a.uptime[4] == b.uptime[4]) {
    if (a.uptime[3] == b.uptime[3]) {
      return a.clientVersion > b.clientVersion;
    } else {
      return a.uptime[3] > b.uptime[3];
    }
  } else {
    return a.uptime[4] > b.uptime[4];
  }
}

bool is_numeric(char *string) {
    int sizeOfString = strlen(string);
    int iteration = 0;
    bool isNumeric = true;

    if (sizeOfString > 0) {
        while(iteration < sizeOfString)
        {
            if (!isdigit(string[iteration]))
            {
                isNumeric = false;
                break;
            }

            iteration++;
        }
    } else {
        isNumeric = false;
    }

    return isNumeric;
}

const char* charReplace(const char *str, char ch1, char ch2)
{
    char *newStr = new char[strlen(str)+1];
    int n = 0;

    while(*str!='\0')
    {
        if (*str == ch1) {
            newStr[n] = ch2;
        } else {
            newStr[n] = *str;
        }
        str++;
        n++;
    }
    newStr[n] = '\0';
    return (const char *)newStr;
}

size_t writeCallback(char* buf, size_t size, size_t nmemb, void* up) {
    for (int c = 0; c<size*nmemb; c++) {
        sCurrentBlock.push_back(buf[c]);
    }
    return size*nmemb; //tell curl how many bytes we handled
}

int readBlockHeightFromExplorer(string sExplorerURL) {
    int nReturn = -1;

    sCurrentBlock = "";
    CURL* curl;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, sExplorerURL.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (!sCurrentBlock.empty() && is_numeric(&sCurrentBlock[0u])) {
        // Block height from explorer was read successfully
        nReturn = std::stoi(sCurrentBlock);
    }

    return nReturn;
}

int hex_string_to_int(std::string sHexString) {
    int x;   
    std::stringstream ss;
    ss << std::hex << sHexString;
    ss >> x;
    return x;
}

extern "C" void* ThreadBlockReader(void*) {
	// Check if block explorer 1 is set
	if (cfg_explorer_url != "") {
		do {
			// Read from block explorer 1
			int nReturnBlock = readBlockHeightFromExplorer(cfg_explorer_url);

			if (nReturnBlock == -1 || nReturnBlock == nCurrentBlock) {
				// Block explorer 1 failed to return a proper block height or the value is the same as the previous value
				// Check if block explorer 2 is set
				if (cfg_explorer_url2 != "") {
					// Save the value from explorer 1
					int nReturnBlockSave = nReturnBlock;
					// Read from block explorer 2
					nReturnBlock = readBlockHeightFromExplorer(cfg_explorer_url2);

					if (nReturnBlockSave == -1 && nReturnBlock == -1) {
						// Block explorer 2 failed to return a proper block height
						nCurrentBlock = nDefaultBlockHeight;
						bCurrentBlockFromExplorer = false;
					} else {
						// Block explorer 2 returned a block height
						// Compare and take the higher value from both block explorers
						nCurrentBlock = (nReturnBlock > nReturnBlockSave ? nReturnBlock : nReturnBlockSave);
						nDefaultBlockHeight = nCurrentBlock;
						bCurrentBlockFromExplorer = true;
					}
				} else {
					// No block explorer 2 is set
					nCurrentBlock = (nReturnBlock == -1 ? nDefaultBlockHeight : nReturnBlock);
					bCurrentBlockFromExplorer = nReturnBlock != -1;
				}
			} else {
				// Block explorer 1 returned a block height
				nCurrentBlock = nReturnBlock;
				nDefaultBlockHeight = nCurrentBlock;
				bCurrentBlockFromExplorer = true;
			}
				
			Sleep(cfg_explorer_requery_seconds * 1000);
		} while(1);
	} else {
		// No block explorers are set so default to getting the hardcoded block height
		nCurrentBlock = nDefaultBlockHeight;
		bCurrentBlockFromExplorer = false;
	}
	return nullptr;
}

extern "C" void* ThreadDumper(void*) {
  int count = 0;
  do {
    Sleep(100000 << count); // First 100s, than 200s, 400s, 800s, 1600s, and then 3200s forever
    if (count < 5)
        count++;
    {
      vector<CAddrReport> v = db.GetAll();
      sort(v.begin(), v.end(), StatCompare);
      FILE *f = fopen("dnsseed.dat.new","w+");
      if (f) {
        {
          CAutoFile cf(f);
          cf << db;
        }
        rename("dnsseed.dat.new", "dnsseed.dat");
      }
      FILE *d = fopen("dnsseed.dump", "w");
	  fprintf(d, "# address                                        good  lastSuccess    %%(2h)   %%(8h)   %%(1d)   %%(7d)  %%(30d)  blocks      svcs  version\n");
      double stat[5]={0,0,0,0,0};
      for (vector<CAddrReport>::const_iterator it = v.begin(); it < v.end(); it++) {
        CAddrReport rep = *it;

		if (fDumpAll) {
			// Show complete list of nodes with all applicable info gathered for each
			char cversionbuffer[7];
			snprintf(cversionbuffer, 7, "%d", rep.clientVersion);
			char blockbuffer[9];
			snprintf(blockbuffer, 9, "%d", rep.blocks);
			fprintf(d, "%-47s  %4d  %11" PRId64 "  %6.2f%% %6.2f%% %6.2f%% %6.2f%% %6.2f%%  %s  %08" PRIx64 "  %s \"%s\"\n", rep.ip.ToString().c_str(), rep.fGood && rep.blocks>0 && rep.clientVersion>0 && strlen(rep.clientSubVersion.c_str())>0?1:0, rep.lastSuccess, 100.0*rep.uptime[0], 100.0*rep.uptime[1], 100.0*rep.uptime[2], 100.0*rep.uptime[3], 100.0*rep.uptime[4], rep.blocks<1?"Unknown":blockbuffer, rep.services, rep.clientVersion<1?"Unknown":cversionbuffer, strlen(rep.clientSubVersion.c_str())==0?"Unknown":rep.clientSubVersion.c_str());
		} else
			fprintf(d, "%-47s  %4d  %11" PRId64 "  %6.2f%% %6.2f%% %6.2f%% %6.2f%% %6.2f%%  %6i  %08" PRIx64 "  %5i \"%s\"\n", rep.ip.ToString().c_str(), (int)rep.fGood, rep.lastSuccess, 100.0*rep.uptime[0], 100.0*rep.uptime[1], 100.0*rep.uptime[2], 100.0*rep.uptime[3], 100.0*rep.uptime[4], rep.blocks, rep.services, rep.clientVersion, rep.clientSubVersion.c_str());

        stat[0] += rep.uptime[0];
        stat[1] += rep.uptime[1];
        stat[2] += rep.uptime[2];
        stat[3] += rep.uptime[3];
        stat[4] += rep.uptime[4];
      }
      fclose(d);
      FILE *ff = fopen("dnsstats.log", "a");
      fprintf(ff, "%llu %g %g %g %g %g\n", (unsigned long long)(time(NULL)), stat[0], stat[1], stat[2], stat[3], stat[4]);
      fclose(ff);
    }
  } while(1);
  return nullptr;
}

extern "C" void* ThreadStats(void*) {
  bool first = true;
  do {
    char c[256];
    time_t tim = time(NULL);
    struct tm *tmp = localtime(&tim);
    strftime(c, 256, "[%y-%m-%d %H:%M:%S]", tmp);
    CAddrDbStats stats;
    db.GetStats(stats);
    if (first)
    {
      first = false;
      printf("\n\n\n\x1b[3A");
    }
    else
      printf("\x1b[2K\x1b[u");
    printf("\x1b[s");
    uint64_t requests = 0;
    uint64_t queries = 0;
    for (unsigned int i=0; i<dnsThread.size(); i++) {
      requests += dnsThread[i]->dns_opt.nRequests;
      queries += dnsThread[i]->dbQueries;
    }
    printf("%s %i/%i available (%i tried in %is, %i new, %i active), %i banned; %llu DNS requests, %llu db queries", c, stats.nGood, stats.nAvail, stats.nTracked, stats.nAge, stats.nNew, stats.nAvail - stats.nTracked - stats.nNew, stats.nBanned, (unsigned long long)requests, (unsigned long long)queries);
    Sleep(1000);
  } while(1);
  return nullptr;
}

string sSeeds[11];
string *seeds = sSeeds;

extern "C" void* ThreadSeeder(void*) {
  do {
    for (int i=0; seeds[i] != ""; i++) {
      vector<CNetAddr> ips;
      LookupHost(seeds[i].c_str(), ips);
      for (vector<CNetAddr>::iterator it = ips.begin(); it != ips.end(); it++) {
        db.Add(CService(*it, cfg_wallet_port), true);
      }
    }
    Sleep(1800000);
  } while(1);
  return nullptr;
}

int main(int argc, char **argv) {
  Config cfg;
  string sConfigName = "settings.conf";
  
  try {
    cfg.readFile(sConfigName.c_str());
  } catch(const FileIOException &fioex) {
    std::cerr << "Error: cannot open " + sConfigName << std::endl;
    return(EXIT_FAILURE);
  } catch(const ParseException &pex) {
    std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
              << " - " << pex.getError() << std::endl;
    return(EXIT_FAILURE);
  }
  
  try {
    cfg_protocol_version = std::stoi(cfg.lookup("protocol_version").c_str());
  } catch(const SettingNotFoundException &nfex) {
    cerr << "Error: Missing 'protocol_version' setting in configuration file." << endl;
	return(EXIT_FAILURE);
  }

  try {
    cfg_init_proto_version = std::stoi(cfg.lookup("init_proto_version").c_str());
  } catch(const SettingNotFoundException &nfex) {
    cerr << "Error: Missing 'init_proto_version' setting in configuration file." << endl;
	return(EXIT_FAILURE);
  }

  try {
    cfg_min_peer_proto_version = std::stoi(cfg.lookup("min_peer_proto_version").c_str());
  } catch(const SettingNotFoundException &nfex) {
    // If the value is not properly set, then default min_peer_proto_version to the protocol_version
    cfg_min_peer_proto_version = cfg_protocol_version;
  }

  try {
    cfg_caddr_time_version = std::stoi(cfg.lookup("caddr_time_version").c_str());
  } catch(const SettingNotFoundException &nfex) {
    cerr << "Error: Missing 'caddr_time_version' setting in configuration file." << endl;
	return(EXIT_FAILURE);
  }

  for (int i=0; i<4; i++) {
	  try {
        cfg_message_start[i] = static_cast<char>(hex_string_to_int(cfg.lookup("pchMessageStart_" + std::to_string(i)).c_str()));
	  } catch(const SettingNotFoundException &nfex) {
		cerr << "Error: Missing 'pchMessageStart_" + std::to_string(i) + "' setting in configuration file." << endl;
		return(EXIT_FAILURE);
	  }
  }

  try {
    cfg_wallet_port = std::stoi(cfg.lookup("wallet_port").c_str());
  } catch(const SettingNotFoundException &nfex) {
    cerr << "Error: Missing 'wallet_port' setting in configuration file." << endl;
	return(EXIT_FAILURE);
  }
  
  try {
    cfg_explorer_url = cfg.lookup("explorer_url").c_str();
  } catch(const SettingNotFoundException &nfex) {
    cfg_explorer_url = "";
  }
  
  try {
    cfg_explorer_url2 = cfg.lookup("second_explorer_url").c_str();
	if (cfg_explorer_url2 != "" && cfg_explorer_url == "") {
		cfg_explorer_url = cfg_explorer_url2;
		cfg_explorer_url2 = "";
	}
  } catch(const SettingNotFoundException &nfex) {
	  cfg_explorer_url2 = "";
  }  

  try {
      if (is_numeric(const_cast<char*>(cfg.lookup("explorer_requery_seconds").c_str()))) {
          cfg_explorer_requery_seconds = std::stoi(cfg.lookup("explorer_requery_seconds").c_str());
          if (cfg_explorer_requery_seconds < 1) {
              cerr << "Error: 'explorer_requery_seconds' setting must be greater than zero." << endl;
              return(EXIT_FAILURE);
          }
      } else {
          // Default to 60 seconds
          cfg_explorer_requery_seconds = 60;
      }
  } catch(const SettingNotFoundException &nfex) {
    if (cfg_explorer_url != "" || cfg_explorer_url2 != "") {
      cerr << "Error: Missing 'explorer_requery_seconds' setting in configuration file." << endl;
      return(EXIT_FAILURE);
    } else {
      cfg_explorer_requery_seconds = 0;
    }
  }

  try {
	nDefaultBlockHeight = std::stoi(cfg.lookup("block_count").c_str());
	nCurrentBlock = nDefaultBlockHeight;
  } catch(const SettingNotFoundException &nfex) {
    cerr << "Error: Missing 'block_count' setting in configuration file." << endl;
	return(EXIT_FAILURE);
  }

  for (int i=1; i<=10; i++) {
	  try {
		sSeeds[i-1] = cfg.lookup("seed_" + std::to_string(i)).c_str();
	  } catch(const SettingNotFoundException &nfex) {
		cerr << "Error: Missing 'seed_0" + std::to_string(i) + "' setting in configuration file." << endl;
		return(EXIT_FAILURE);
	  }
  }

  signal(SIGPIPE, SIG_IGN);
  setbuf(stdout, NULL);
  CDnsSeedOpts opts;
  opts.ParseCommandLine(argc, argv);
  printf("Supporting whitelisted filters: ");
  for (std::set<uint64_t>::const_iterator it = opts.filter_whitelist.begin(); it != opts.filter_whitelist.end(); it++) {
      if (it != opts.filter_whitelist.begin()) {
          printf(",");
      }
      printf("0x%lx", (unsigned long)*it);
  }
  printf("\n");
  if (opts.tor) {
    CService service(opts.tor, 9050);
    if (service.IsValid()) {
      printf("Using Tor proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_TOR, service);
    }
  }
  if (opts.ipv4_proxy) {
    CService service(opts.ipv4_proxy, 9050);
    if (service.IsValid()) {
      printf("Using IPv4 proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_IPV4, service);
    }
  }
  if (opts.ipv6_proxy) {
    CService service(opts.ipv6_proxy, 9050);
    if (service.IsValid()) {
      printf("Using IPv6 proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_IPV6, service);
    }
  }
  if (strcmp(opts.force_ip, "A") != 0 && strcmp(opts.force_ip, "a") != 0 && strcmp(opts.force_ip, "4") != 0 && strcmp(opts.force_ip, "6") != 0) {
    fprintf(stderr, "Invalid force ip option. Valid options are: a = all (default), 4 = IPv4, 6 = IPv6.\n");
    exit(1);
  }
  bool fDNS = true;
  if (!opts.ns) {
    printf("No nameserver set. Not starting DNS server.\n");
    fDNS = false;
  }
  if (fDNS && !opts.host) {
    fprintf(stderr, "No hostname set. Please use -h.\n");
    exit(1);
  }
  if (opts.mbox == NULL) {
    // No email set. Initialize to "" string
    opts.mbox = "";
  } else {
    // Email is set. Replace "@" with "."
    opts.mbox = charReplace(opts.mbox, '@', '.');
  }
  FILE *f = fopen("dnsseed.dat","r");
  if (f) {
    printf("Loading dnsseed.dat...");
    CAutoFile cf(f);
    cf >> db;
    if (opts.fWipeBan)
        db.banned.clear();
    if (opts.fWipeIgnore)
        db.ResetIgnores();
    printf("done\n");
  }
  fDumpAll = opts.fDumpAll;
  sForceIP.assign(opts.force_ip, strlen(opts.force_ip));
  pthread_t threadBlock, threadDns, threadSeed, threadDump, threadStats;
  printf("Starting block reader...");
  pthread_create(&threadBlock, NULL, ThreadBlockReader, NULL);
  printf("done\n");
  if (fDNS) {
    printf("Starting %i DNS threads for %s on %s (port %i)...", opts.nDnsThreads, opts.host, opts.ns, opts.nPort);
    dnsThread.clear();
    for (int i=0; i<opts.nDnsThreads; i++) {
      dnsThread.push_back(new CDnsThread(&opts, i));
      pthread_create(&threadDns, NULL, ThreadDNS, dnsThread[i]);
      printf(".");
      Sleep(20);
    }
    printf("done\n");
  }
  printf("Starting seeder...");
  pthread_create(&threadSeed, NULL, ThreadSeeder, NULL);
  printf("done\n");
  printf("Starting %i crawler threads...", opts.nThreads);
  pthread_attr_t attr_crawler;
  pthread_attr_init(&attr_crawler);
  pthread_attr_setstacksize(&attr_crawler, 0x20000);
  for (int i=0; i<opts.nThreads; i++) {
    pthread_t thread;
    pthread_create(&thread, &attr_crawler, ThreadCrawler, &opts.nThreads);
  }
  pthread_attr_destroy(&attr_crawler);
  printf("done\n");
  pthread_create(&threadStats, NULL, ThreadStats, NULL);
  pthread_create(&threadDump, NULL, ThreadDumper, NULL);
  void* res;
  pthread_join(threadDump, &res);
  return 0;
}
