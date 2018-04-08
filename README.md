C-bittrex
==============
Bittrex C API command line - **use at your own risk**.

This is a command line to use bittrex API.

It also contains an example of a bot for trading based on RSI 14 (in minute and hour).

The API is functional, the bot mode is still experimental **use at your own risk**.
Moreover, during my tests I discovered that bittrex API replies with delayed candles (getticks), between 1 and 5mn delay. So, again, **use at your own risk**

I suggest to use the CLI to script some monitoring on your favorite coins/market and put it in a crontab.
So if a long term opportunity shows up you can get an email without checking all the time the candles :)


Todo
-------------
What's left to do:
- add a makefile and automatic tests (tests added)
- add a new call : --volumeonrange start_date end_date (buy and sell detailed volumes)


Fixed or added recently:
-------------
- store bot orders in a database: **done**
- In case of crash or program termination, the bot needs to be aware of its last state and resume (waiting to buy or to sell and corresponding orders for each thread running): **done**
- limit API call to 1/s per type of call : **done** (mostly usefull for the bot)
- Protect MySQL connector and bittrex_info fields modified by bot threads with a lock: **done**
- Valgrind on most calls (not the bot) **done**
- added --getrsi and --getema in the CLI **done**
- add a thread scanning input for bot mode in order to be able to stop it properly (so far in bot mode, you need to kill with Ctrl+C) **done**

Installation
-------------

You need to have jansson installed on your OS (http://www.digip.org/jansson/) and MySQL (server + client).

On CentOS:
```
yum install jansson.x86_64
Optional: jansson-devel.x86_64
```
Then just compile with:

```
gcc -W -Wall -lpthread -l curl -l jansson market.c main.c bittrex.c trade.c account.c bot.c lib/hmac/hmac_sha2.c lib/hmac/sha2.c -g -o bittrex  `mysql_config --libs`
```

Setup the database by sourcing the sql file (consider to change the password in *installdb.sql* and *bittrex.h*)

API key file is just a file with two lines:

```
line 1 is the key
line 2 is the secret
```


Bittrex API Documentation
-------------
**API 1.1 is considered stable**

[Official API Documentation](https://bittrex.com/Home/Api)

**API 2.0 is BETA, use at your own risk**

For V2 API, I used this page as reference:
https://github.com/dparlevliet/node.bittrex.api#supported-v2-api-methods

Usage
-------------
```
Usage: ./bittrex [OPTIONS] apicall
 -a, --apikeyfile       path to api key file
 -m, --market   specify a market
 -c, --currency  specify a currency
 -h, --help     print help
 -s, --stats    print stats only
 -b, --bot      trading bot, requires -a
Public API calls:
 ./bittrex [--getmarkets|--getcurrencies|--getmarketsummaries]
 ./bittrex --market=marketname --getticker||--getmarketsummary||--getmarkethistory
 ./bittrex --market=marketname --getorderbook both|buy|sell
 ./bittrex --market=marketname --getticks oneMin|fiveMin|thirtyMin|Hour
 ./bittrex --market=marketname --getema oneMin|fiveMin|thirtyMin|Hour,period
 ./bittrex --market=marketname --getrsi oneMin|fiveMin|thirtyMin|Hour,period
Market API Calls:
 ./bittrex --apikeyfile=path --market=marketname --buylimit|--selllimit|--tradebuy|--tradesell quantity,rate
 ./bittrex --apikeyfile=path --market=marketname --cancel orderuuid
Account API Calls:
 ./bittrex --apikeyfile=path --getbalances
 ./bittrex --apikeyfile=path --currency=coin --getbalance|--getdepositaddress
 ./bittrex --apikeyfile=path --currency=coin --withdraw destinationAddress
 ./bittrex --apikeyfile=path --getorder orderuuid
 ./bittrex --apikeyfile=path --market=marketname --getopenorders
 ./bittrex --apikeyfile=path [--market=marketname] --getorderhistory
 ./bittrex --apikeyfile=path --currency=coin --getwithdrawalhistory
 ./bittrex --apikeyfile=path --currency=coin --getdeposithistory`
```
The API keyfile must be on two lines containing only the key and the secret:
```
line1: apikey
line2: apisecret
```

**If you intend to use the CLI to pass orders (or the bot), you must be sure that nobody can access this file.
I strongly advise to not run this CLI/bot on a shared server if you are not the admin/root.**

Examples of calls
-------------
* Valid buy:
```
./bittrex -a ~/apikey --market=BTC-XVG --buylimit 250,0.00000355
UUID: f18598ea-c807-4bc0-a5a5-774e3cbf7ede
```
* Invalid buy (minimum trade size not met or bad api permission or anything that went wront):
```
./bittrex -a ~/apikey --market=BTC-XVG --buylimit 10,0.00000250
Error proccessing request: https://bittrex.com/api/v1.1/market/buylimit?apikey=removed_from_example&market=BTC-XVG&quantity=10.00000000&rate=0.00000250&nonce=1521408865
API replied: MIN_TRADE_REQUIREMENT_NOT_MET
```
* Valid sell:
```
./bittrex -a ~/apikey --market=BTC-XVG --selllimit 250,0.00000369
UUID: 167bc9b7-11f7-403a-b6f5-50cf30b8e326
```
* GetOrder:
```
./bittrex -a ~/apikey --getorder 167bc9b7-11f7-403a-b6f5-50cf30b8e326
UUID:                           167bc9b7-11f7-403a-b6f5-50cf30b8e326
Exchange:                       BTC-XVG
OrderType:                      LIMIT_SELL
Quantity:                       250.00000000
QuantityRemaining:              250.00000000
Limit:                          0.000004
Reserved:                       250.00000000
ReservedRemaining:              0.00000000
CommissionReserved:             0.00000000
CommissionReservedRemaining:    0.00000000
CommissionPaid:                 0.00000000
Price:                          0.00000000
PricePerUnit:                   0.00000000
Opened:                         2018-03-18T21:59:52.08
IsOpen:                         true
CancelInitiated:                false
ImmediateOrCancel:              false
IsConditional:                  false
Condition:                      NONE
ConditionTarget:                (null)
```
* Cancel the order:
```
./bittrex -a ~/apikey --market=BTC-XVG --cancel 167bc9b7-11f7-403a-b6f5-50cf30b8e326
Order: 167bc9b7-11f7-403a-b6f5-50cf30b8e326 canceled
```
Check again with getorder, IsOpen should have changed to false
```
./bittrex -a ~/apikey --getorder 167bc9b7-11f7-403a-b6f5-50cf30b8e326
UUID:                           167bc9b7-11f7-403a-b6f5-50cf30b8e326
Exchange:                       BTC-XVG
OrderType:                      LIMIT_SELL
Quantity:                       250.00000000
QuantityRemaining:              250.00000000
Limit:                          0.000004
Reserved:                       250.00000000
ReservedRemaining:              0.00000000
CommissionReserved:             0.00000000
CommissionReservedRemaining:    0.00000000
CommissionPaid:                 0.00000000
Price:                          0.00000000
PricePerUnit:                   0.00000000
Opened:                         2018-03-18T21:59:52.08
IsOpen:                         false
CancelInitiated:                false
ImmediateOrCancel:              false
IsConditional:                  false
Condition:                      NONE
ConditionTarget:                (null)
```

"Special" calls
-------------

* Withdraw call will ask input confirmation:
```
./bittrex -a ~/apikey -c XVG --withdraw 42.42,D6SRq71nRDurFvayU2tVxfWnbUGA3WvRmf
You are about to send 42.420000 of XVG to D6SRq71nRDurFvayU2tVxfWnbUGA3WvRmf.
Are you sure to proceed? (y/n)
```

* Some calls use bittrex API V2. It may change in the future (not stable version of bittrex API, stable version is API V1.1) so could break.
```
GETTICKS (implemented and used by bot)
TRADEBUY (not implemented yet)
TRADESELL (not implemented yet)
```

Bot
-------------
Please refer on this wiki page (https://github.com/jbriaux/c-bittrex/wiki/Bot-documentation)

Libraries
-------------

The json parser used is jansson (http://www.digip.org/jansson/)

HMAC part is based on this implementation: https://github.com/ogay/hmac/

Just modified it to remove unecessary parts (kept only hmac-512) and adapted to my needs.
I had a working version with RedHat implementation of hmac but was not sure about the license in the end.
And there were too many libraries to link (-lssl3 -lsmime3 -lnss3 -lnssutil3 -lplds4 -lplc4 -lnspr4).

You may have noticed the -lpthread option, this is necessary for bot mode.
In bot mode, the program starts by sorting markets by volume and select most intersting markets (tbh I need to change this part) and create one thread for each selected market.

As in bot mode, multiple threads run in parallel, there are locks on specific area because mysqlclient is not thread safe, curl_global_init is not thread safe and some structures can be accessed(modfied) in parallel (bittrex_info struct which stores and share the number of active trades between threads).

Init to do if you intend to develop for your own use
-------------

First thing to do is to init bittrex env, markets and currencies available.
```
struct bittrex_info *bi;
bi = bittrex_info();
bi->markets = getmarkets();
bi->currencies = getcurrencies();
```

Then you can do whatever you want (API replies are parsed and stored in structures)
