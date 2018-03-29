/*
 * MIT License
 *
 * Copyright (c) 2018 Jean-Baptiste Riaux <jb.riaux@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <pthread.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>

#include "lib/jansson/src/jansson.h"
#include "lib/hmac/hmac_sha2.h"

#include "market.h"
#include "bittrex.h"
#include "account.h"
#include "bot.h"

static void print_help(char *arg) {
	if (!arg || strlen(arg) == 0) {
		printf("Usage: ./bittrex [OPTIONS] apicall\n");
		printf(" -a, --apikeyfile\tpath to api key file\n");
		printf(" -m, --market\tspecify a market\n");
		printf(" -c, --currency\t specify a currency\n");
		printf(" -h, --help\tprint help\n");
		printf(" -s, --stats\tprint stats only\n");
		printf(" -b, --bot\ttrading bot, requires -a\n");
		printf("Public API calls:\n");
		printf(" ./bittrex [--getmarkets|--getcurrencies|--getmarketsummaries]\n");
		printf(" ./bittrex --market=marketname --getticker||--getmarketsummary||--getmarkethistory\n");
		printf(" ./bittrex --market=marketname --getorderbook both|buy|sell\n");
		printf(" ./bittrex --market=marketname --getticks oneMin|fiveMin|thirtyMin|Hour\n");
		printf(" ./bittrex --market=marketname --getema oneMin|fiveMin|thirtyMin|Hour,period\n");
		printf(" ./bittrex --market=marketname --getrsi oneMin|fiveMin|thirtyMin|Hour,period\n");
		printf("Market API Calls:\n");
		printf(" ./bittrex --apikeyfile=path --market=marketname --buylimit|--selllimit|--tradebuy|--tradesell quantity,rate\n");
		printf(" ./bittrex --apikeyfile=path --market=marketname --cancel orderuuid\n");
		printf("Account API Calls:\n");
		printf(" ./bittrex --apikeyfile=path --getbalances\n");
		printf(" ./bittrex --apikeyfile=path --currency=coin --getbalance|--getdepositaddress\n");
		printf(" ./bittrex --apikeyfile=path --currency=coin --withdraw destinationAddress\n");
		printf(" ./bittrex --apikeyfile=path --getorder orderuuid\n");
		printf(" ./bittrex --apikeyfile=path --market=marketname --getopenorders\n");
		printf(" ./bittrex --apikeyfile=path [--market=marketname] --getorderhistory \n");
		printf(" ./bittrex --apikeyfile=path --currency=coin --getwithdrawalhistory \n");
		printf(" ./bittrex --apikeyfile=path --currency=coin --getdeposithistory \n");
		exit(0);
	}
	if (strcmp(arg, "--buylimit") == 0) {
		printf("--buylimit quantity,rate\n");
	}
	if (strcmp(arg, "--selllimit") == 0) {
		printf("--selllimit quantity,rate\n");
	}
	if ((strcmp(arg, "--apikeyfile") == 0) || (strcmp(arg, "-a") == 0)) {
		printf("--apikeyfile path_to_api_key_file\n");
		printf("Key file must be formated as follow:\n");
		printf("line1: API Key\nline2: API Secret\n");
	}
	if (strcmp(arg, "--getticks") == 0) {
		printf("getticks requires tick interval value: oneMin, fiveMin, thirtyMin, Hour\n");
		printf("ex: ./bittrex --market=BTC-XVG --getticks oneMin\n");
	}
	if (strcmp(arg, "--getorderbook") == 0) {
		printf("getorderbook type invalid, use : both, buy or sell\n");
		printf("ex: ./bittrex -m BTC-XVG --getorderbook sell\n");
	}
	exit(EINVAL);
}

static void arg_required(char *call, char *arg) {
	fprintf(stderr, "%s requires %s\n", call, arg);
	exit(EINVAL);
}

static int orderbookType_is_valid(char *s) {
	if (!s)
		return 0;
	if ((strcmp(s, "buy") == 0) ||
	    (strcmp(s, "sell") == 0) ||
	    (strcmp(s, "both") == 0))
		return 1;
	return 0;
}

static int tickInterval_is_valid(char *s) {
	if (!s)
		return 0;
	if ((strcmp(s, "oneMin") == 0) ||
	    (strcmp(s, "fiveMin") == 0) ||
	    (strcmp(s, "thirtyMin") == 0) ||
	    (strcmp(s, "Hour") == 0))
		return 1;
	return 0;
}

/*
 * return the number of commas in string
 * fixme replace by regexp
 */
static int nbc(char *s) {
	int i = 0;
	int nb = 0;

	while (s[i]) {
		if (s[i] == ',')
			nb++;
		i++;
	}
	return nb;
}

int main(int argc, char *argv[]) {
	struct bittrex_info *bi;
	struct market *market = NULL;
	struct ticker *tick = NULL;
	struct tick **ticks = NULL;
	struct currency *c = NULL;
	struct api *api = NULL;
	struct user_order *o = NULL, **orders = NULL;
	struct balance **balances = NULL, *b = NULL;
	struct deposit **d = NULL;
	FILE *file = NULL;
	char *apikey = NULL, *call = NULL, *orderbooktype = NULL;
	char *tickinterval = NULL, *uuid = NULL, *destaddress = NULL;
	char *da = NULL;
	char *paymentid = NULL;
	char *interval = NULL;
	char opt, key[33], secret[33];
	char buf[255], buf2[32];
	double quantity = -1, rate = -1;
	double *ma;
	int period = 0;
	int opt_index;
	int api_required = 0, market_required = 0, currency_required = 0;
	static int action_flag = -1;

	static struct option long_options[] = {
		/* general options */
		{"apikeyfile",		required_argument,	0, 'a'}, // API key file
		{"market",		required_argument,	0, 'm'}, // Market
		{"currency",		required_argument,	0, 'c'}, // Currency
		{"help",		no_argument,		0, 'h'}, // print help
		{"stats",		no_argument,		0, 's'}, // just print stats about top markets in volume

		/* bot mode, api key required */
		{"bot",			no_argument,		0, 'b'}, // bot mode

		/* public API */
		{"getmarkets",		no_argument,		0,  0 },
		{"getcurrencies",	no_argument,		0,  0 },
		{"getmarketsummaries",	no_argument,		0,  0 },
		{"getticker",		no_argument,		0,  1 }, // market opt set
		{"getmarketsummary",	no_argument,		0,  1 }, // market opt set
		{"getorderbook",	required_argument,	0,  1 }, // market opt set arg is both,sell or buy
		{"getmarkethistory",	no_argument,		0,  1 }, // market opt set
		{"getticks",		required_argument,	0,  2 }, //api V2 getcandles, market, tickInterval

		/* market API, api key required */
		{"buylimit",		required_argument,      0,  3 }, // V1 market, quantity, rate
		{"selllimit",		required_argument,      0,  3 }, // V1 market, quantity, rate
		{"tradebuy",		required_argument,      0,  3 }, // V2 market, quantity, rate
		{"tradesell",		required_argument,      0,  3 }, // V2 market, quantity, rate
		{"cancel",		required_argument,      0,  4 }, // order uuid

		/* account API, api key required */
		{"getbalances",		no_argument,		0,  5 },
		{"getbalance",		no_argument,		0,  6 }, // currency
		{"getdepositaddress",	no_argument,		0,  6 }, // currency
		{"withdraw",		required_argument,	0,  7 }, // currency quantity address opt payment id
		{"getorder",		required_argument,	0,  8 }, // order uuid
		{"getorderhistory",	no_argument,		0,  9 }, // market optional
		{"getwithdrawalhistory",no_argument,		0,  10 }, // currency optional
		{"getdeposithistory",	no_argument,		0,  10 }, // currency optional
		{"getopenorders",	no_argument,		0,  11 }, // currency optional

		/* indicators */
		{"getema",		required_argument,	0,  13 }, // exponantial moving average
		{"getrsi",		required_argument,	0,  13 }, // RSI

		/* help */
		{"help",		no_argument,		0, 'h'},
		{0,           0,                 0,  0   }
	};

	// init bittrex_info struct
	bi = bittrex_info();

	/*
	 * option rules
	 * Here we set some flags if specific options are required.
	 */
	opterr = 0;
	while ((opt = getopt_long(argc, argv, "a:m:c:bh", long_options, &opt_index)) != -1) {
		switch (opt) {
		case 0: // public API no args
			action_flag = 0;
			call = argv[optind-1];
			break;
		case 1: // public API market arg
			action_flag = 1;
			market_required = 1;
			call = argv[optind-1];
			if (optarg) { // getorderbook
				orderbooktype = optarg;
				call = argv[optind-2];
				if (!orderbookType_is_valid(orderbooktype))
					print_help(call);
			}
			break;
		case 2: // getticks
			market_required = 1;
			action_flag = 2;
			call = argv[optind-1];
			if (optarg) {
				tickinterval = optarg;
				call = argv[optind-2];
				if (!tickInterval_is_valid(tickinterval))
					print_help(call);
			}
			break;
		case 3: //buylimit, selllimit, tradebuy, tradesell
			//fixme add options
			api_required = market_required = 1;
			action_flag = 3;
			call = argv[optind-2];
			int rc;
			/* to test with %[^,] fixme? */
			if (nbc(optarg) != 1) {
				fprintf(stderr, "Invalid numerical argument detected\n");
				fprintf(stderr, "Sorry but numbers must use a period '.' (US/UK style) not a comma ','");
				fprintf(stderr, "(rest of the world style)\n");
				exit(EINVAL);
			}
			rc = sscanf(optarg, "%lf,%lf", &quantity, &rate);
			if (rc !=  2 || quantity < 0 || rate < 0) {
				fprintf(stderr, "Invalid Quantity or Rate specified\n");
				print_help(call);
			}
			break;
		case 4: // cancel fixme
			api_required = 1;
			action_flag = 4;
			uuid = optarg;
			call = argv[optind-2];
			break;
		case 5: // getbalances
			api_required = 1;
			action_flag = 5;
			call = argv[optind-1];
			break;
		case 6: // getbalance getdepositaddress
			call = argv[optind-1];
			action_flag = 6;
			api_required = currency_required = 1;
			break;
		case 7: //withdraw
			api_required = currency_required = 1;
			action_flag = 7;
			call = argv[optind - 2];
			if (nbc(optarg) == 1) { // no paymentid
				if (sscanf(optarg, "%lf,%[^,]", &quantity, buf) != 2) {
					fprintf(stderr, "Invalid Quantity or Destination address specified\n");
					print_help(call);
				}
			} else {
				if (sscanf(optarg, "%lf,%[^,],%[^,]", &quantity, buf, buf2) != 3) {
					fprintf(stderr, "Invalid Quantity, Destination address or paymentID specified\n");
					print_help(call);
				}
				paymentid = malloc(strlen(buf2) +1);
				paymentid = strcpy(paymentid, buf2);
			}
			destaddress = malloc(strlen(buf)+1);
			destaddress = strcpy(destaddress, buf);
			break;
		case 8: //getorder
			api_required = 1;
			action_flag = 8;
			call = argv[optind-2];
			uuid = optarg;
			break;
		case 9: //getorderhistory
			api_required = 1;
			action_flag = 9;
			call = argv[optind-1];
			break;
		case 10: //getwithdrawalhistory and getdeposithistory
			api_required = 1;
			action_flag = 10;
			currency_required = 1;
			call = argv[optind-1];
			break;
		case 11: //getopenorders
			api_required = 1;
			action_flag = 11;
			break;
		case 13:
			market_required = 1;
			call = argv[optind-2];
			action_flag = 13;
			if (nbc(optarg) == 1) {
				sscanf(optarg, "%[^,],%d", buf, &period);
				interval = malloc(strlen(buf) + 1);
				strcpy(interval, buf);
				if (!tickInterval_is_valid(interval))
					print_help(call);
			}
			break;
		case 'a':
			apikey = optarg;
			file = fopen(apikey, "r");
			if (!file) {
				fprintf(stderr, "File %s could not be opened\n", apikey);
				exit(EINVAL);
			}
			fgets(key, 34, file);
			if (key[32] == '\n')
				key[32] = '\0';
			fgets(secret, 33, file);
			if (secret[32] == '\n')
				secret[32] = '\0';
			fclose(file);
			api = new_api(key, secret);
			if (!api) {
				fprintf(stderr, "Invalid API keyfile specified: %s\n", optarg);
				exit(EINVAL);
			}
			bi->api = api;
			break;
		case 'm':
			getmarkets(bi);
			market = getmarket(bi->markets, optarg);
			if (!market) {
				fprintf(stderr, "Invalid market specified: %s\n", optarg);
				exit(EINVAL);
			}
			break;
		case 'c':
			bi->currencies = getcurrencies(bi);
			c = getcurrency(bi->currencies, optarg);
			if (!c) {
				fprintf(stderr, "Invalid currency specified: %s\n", optarg);
				exit(EINVAL);
			}
			break;
		case 'b': //bot mode
			api_required = 1;
			action_flag = 12;
			call = argv[optind-1];
			break;
		case 's': //statistics
			break;
		case 'h':
			print_help("");
			break;
		case '?':
			printf("Invalid argument %s\n", argv[optind-1]);
			print_help(argv[optind-1]);
			break;
		default:
			print_help("");
			exit(EINVAL);
		}

	}

	/*
	 * Check required arguments flags
	 */
	if (api_required && !api) {
		arg_required(call, "API key (-a || -apikeyfile)");
	}

	if (market_required && !market) {
		arg_required(call, "market (-m || --market)");
	}

	if (currency_required && !c) {
		arg_required(call, "currency (-c || --currency)");
	}

	/*
	 * Perform requested action
	 */
	switch (action_flag) {
	case 0:
		if (strcmp(call, "--getmarkets") == 0) {
			getmarkets(bi);
			printmarkets(bi->markets);
		}
		if (strcmp(call, "--getcurrencies") == 0) {
				bi->currencies = getcurrencies(bi);
				printcurrencies(bi->currencies);
		}
		if (strcmp(call, "--getmarketsummaries") == 0) {
			getmarketsummaries(bi);
			printmarketsummaries(bi->markets);
		}
		break;
	case 1:
		if (strcmp(call, "--getticker") == 0) {
			tick = getticker(bi, market);
			printticker(tick);
		}
		if (strcmp(call, "--getmarketsummary") == 0) {
			getmarketsummary(bi, market);
			printmarketsummary(market);
		}
		if (strcmp(call, "--getorderbook") == 0) {
			getorderbook(bi, market, orderbooktype);
			printorderbook(market);
		}
		if (strcmp(call, "--getmarkethistory") == 0) {
			getmarkethistory(bi, market);
			printmarkethistory(market);
		}
		break;
	case 2:	/* getticks */
		printf("Newest to oldest\n");
		printticks((ticks = getticks(bi, market, tickinterval, 100, ASCENDING)));
		free_ticks(ticks);
		break;
	case 3:	/* trades */
		if (strcmp(call, "--buylimit") == 0) {
			// API V1.1
			buylimit(bi, market, quantity, rate);
		}
		if (strcmp(call, "--selllimit") == 0) {
			//API V1.1
			selllimit(bi, market, quantity, rate);
		}
		if (strcmp(call, "--tradebuy") == 0) {
			/* fixme API V2*/
		}
		if (strcmp(call, "--tradesell") == 0) {
			/* fixme API V2*/
		}
		break;
	case 4:	/* cancel order */
		cancel(bi, uuid);
		break;
	case 5:	/* getbalances */
		printbalances((balances = getbalances(bi, api)));
		free_balances(balances);
		break;
	case 6:	/* getbalance, getdepositaddress */
		if (!c)
			arg_required(call, "currency");
		if (strcmp(call, "--getbalance") == 0) {
			printbalance((b = getbalance(bi, c,api)));
			free_balance(b);
		}
		if (strcmp(call, "--getdepositaddress") == 0) {
			printf("Currency %s deposit address: %s\n", c->coin, (da = getdepositaddress(bi, c, api)));
			free(da);
		}
		break;
	case 7:	/* withdraw */
		printf("You are about to send %lf of %s to %s.\n", quantity, c->coin, destaddress);
		int vc = 0;
		char cval = '0';
		while (vc == 0) {
			printf("Are you sure to proceed? (y/n)\n");
			scanf("%c", &cval);
			cval = tolower(cval);
			if (cval == 'y') {
				withdraw(bi, c, quantity, destaddress, paymentid);
				return 0;
			}
			if (cval == 'n')
				return 0;
			else {
				printf("Invalid choice: y or n\n");
				while (cval != '\n')
					cval = getchar();
			}
			cval = '0';
		}
		break;
	case 8:	/* getorder */
		printorder((o = getorder(bi, uuid)));
		if (o)
			free_user_order(o);
		break;
	case 9:	/* getorderhistory */
		if (!market) // no market specified
			getmarkets(bi);
		printorders((orders = getorderhistory(bi, market)));
		free_user_orders(orders);
		break;
	case 10: /* getwithdrawalhistory getdeposithistory */
		if (!c)
			bi->currencies = getcurrencies(bi);
		if (strcmp(call, "--getwithdrawalhistory") == 0) {
			getwithdrawalhistory(bi, c);
		}
		if (strcmp(call, "--getdeposithistory") == 0) {
			printdeposithistory((d = getdeposithistory(bi, c)));
			free_deposits(d);
		}
		break;
	case 11: /* getopenorders */
		if (!market)
			getmarkets(bi);
		printorders((orders = getopenorders(bi, market)));
		free_user_orders(orders);
		break;
	case 12: /* bot */
		getmarkets(bi);
		if (!conn_init(bi)) {
			fprintf(stderr, "Connection to MySQL failed\n. Exiting.\n");
			exit(EPERM);
		}
		getmarketsummaries(bi);
		bi->currencies = getcurrencies(bi);
		bot(bi);
		break;
	case 13: /* EMA */
		if (strcmp(call, "--getema") == 0) {
			ma = ema_interval_period(bi, market, interval, period);
			printf("%.8f\n", ma[period-1]);
			free(ma);
		}
		if (strcmp(call, "--getrsi") == 0) {
			printf("%.4f\n", rsi_mma_interval_period(bi, market, interval, period));
			//getticks_rsi_mma_interval_period(bi, market, interval, period);
			//rsi_interval_period(bi, market, interval, period);
		}
		free(interval);
		break;
	default:
		printf("No command specified.\n./bittrex --help for help\n");
		return 0;
	}

	free_bi(bi);

	return 0;
}
