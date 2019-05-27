import logging
import sys

import config
import cf
import errors
import parser
import string
import os

MAX_SEEDS = 25

logger = logging.getLogger(__name__)


def main():

    """ Main entry point. """

    configuration = config.read_local_config()

    logging.basicConfig(stream=sys.stdout, level=logging.DEBUG)

    try:
        seed_candidates = parser.read_seed_dump(os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + '/' + string.replace(configuration['cf_seed_dump'], '"', ''), string.replace(configuration['wallet_port'], '"', '')[:7].strip())
    except errors.SeedsNotFound as e:
        print("ERROR: Problem reading seeds - {}".format(e.message))
        sys.exit(-1)

    cloudflare = cf.CloudflareSeeder.from_configuration(configuration)
    current_seeds = cloudflare.get_seeds()

    logger.debug("Detected current seeds in cloudflare: {}".format(current_seeds))

    # Remove stale seeds (not in our candidate list
    stale_current_seeds = [seed for seed in current_seeds if seed not in seed_candidates]
    if stale_current_seeds:
        cloudflare.delete_seeds(stale_current_seeds)
        current_good_seeds = [seed for seed in current_seeds if seed not in stale_current_seeds]
    else:
        current_good_seeds = current_seeds

    # Prune
    if len(current_good_seeds) >= MAX_SEEDS:
        deleting = [seed for seed in current_good_seeds if seed not in seed_candidates]
        if deleting:
            cloudflare.delete_seeds(deleting)
            current_good_seeds = [seed for seed in current_good_seeds if seed not in deleting]

    # Grow
    shortfall = MAX_SEEDS - len(current_good_seeds)
    to_add = []
    for seed in seed_candidates:
        if len(to_add) >= shortfall:
            break
        if seed not in current_good_seeds:
            to_add.append(seed)

    if len(to_add):
        cloudflare.set_seeds(to_add)


if __name__ == "__main__":
    main()