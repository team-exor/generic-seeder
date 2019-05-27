""" Cloudflare interface """
import logging
import CloudFlare
import errors
import string

logger = logging.getLogger(__name__)


def isipv6(ip):

    """ Extremely naive IPV6 check. """

    return ip.count(':') > 1

def _lookup_zone_id(cloudflare, domain):

    """ Return the zone_id for a given domain using the cloudflare interface. """

    logger.info("Resolving cloudflare zoneid for domain name: ".format(domain))
    zones = cloudflare.zones.get(params={'name': domain})

    if not len(zones):
        raise errors.ZoneNotFound("Could not find zone named: {}".format(domain))

    if len(zones) > 1:
        raise errors.TooManyZones("More than one zone found named: {}".format(domain))

    return zones[0]['id']


class CloudflareSeeder(object):

    """ Cloudflare abstraction layer allowing to manage DNS entries. """

    @staticmethod
    def from_configuration(configuration):

        """" Instantiate and return an instance from a configuration dict. """

        logger.debug("Creating CloudflareSeeder interface from configuration.")

        user = string.replace(configuration['cf_username'], '"', '')
        key = string.replace(configuration['cf_api_key'], '"', '')
        domain = string.replace(configuration['cf_domain'], '"', '')
        name = string.replace(configuration['cf_domain_prefix'], '"', '')

        return CloudflareSeeder(user, key, domain, name)

    def __init__(self, user, key, domain, name):

        """ Constructor: set the member variables. """

        logger.debug("CloudflareSeeder creation for user: {} domain: {} name: {}".format(user, domain, name))
        self.cf = CloudFlare.CloudFlare(email=user, token=key)
        self.domain = domain
        self.name = name
        self._zone_id = None

    @property
    def zone_id(self):

        """ Resolve the zone id from the name if we haven't before. If we have, just return it. """

        if self._zone_id is None:
            self._zone_id = _lookup_zone_id(self.cf, self.domain)

        return self._zone_id

    def get_seed_records(self, flags=False):

        """ Get the seed dns records, i.e., those which are type A or AAAA and match the name. """

        page = 0
        records = []
        zone_id = self.zone_id

        name_parts = [self.name, self.domain]

        if flags:
            name_parts.insert(0, 'x9')

        default_params = {'name': '.'.join(name_parts), 'type': 'A,AAAA', 'per_page': 10}

        self.cf._base.raw = True
        while True:
            page += 1
            default_params['page'] = page

            logger.info("Getting page {} of DNS entries".format(page))

            raw_results = self.cf.zones.dns_records.get(zone_id, params=default_params)
            records.extend(raw_results['result'])

            num_pages = raw_results['result_info']['total_pages']
            if page == num_pages or num_pages == 0:
                logger.debug("Fetched all pages of DNS seeds in cloudflare.")
                break

        self.cf._base.raw = False

        return records

    def get_seeds(self):

        """ Read the seeds for the zone and name in cloudflare. """

        logger.debug("Getting seeds from cloudflare")
        return [record['content'] for record in self.get_seed_records()]

    def _set_seed(self, seed, ttl=None, flags=False):

        """ Set either a flags or no flags seed entry in cloud flare. """

        logger.debug("Setting seed {} in cloudflare".format(seed))
        new_record = {'name': self.name if not flags else 'x9.' + self.name, 'type': 'AAAA' if isipv6(seed) else 'A', 'content': seed}

        if ttl is not None:
            new_record['ttl'] = ttl

        logger.debug("Posting record {}".format(new_record))
        try:
            self.cf.zones.dns_records.post(self.zone_id, data=new_record)
        except CloudFlare.exceptions.CloudFlareAPIError as e:
            logger.error("Error setting seed through the cloudflare API: %d %s"%(e, e))

    def set_seed(self, seed, ttl=None):

        """ Add a new seed record to cloudflare with corresponding flagged entry. """

        self._set_seed(seed, ttl=ttl)
        self._set_seed(seed, ttl=ttl, flags=True)

    def delete_seeds(self, seeds):

        """ Delete the seeds' DNS entries in cloudflare. """

        logger.debug("Deleting seeds from cloudflare.")
        for seed_record in self.get_seed_records() + self.get_seed_records(flags=True):
            if seed_record['content'] in seeds:
                logger.debug("Found seed to delete: {}".format(seed_record['content']))
                self.cf.zones.dns_records.delete(self.zone_id, seed_record['id'])

    def set_seeds(self, seeds, ttl=None):

        """ Set a list of seeds as DNS entries in cloudflare. """

        for seed in seeds:
            self.set_seed(seed, ttl)

