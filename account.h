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

#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <time.h>
#include "bittrex.h"
#include "market.h"

struct api {
	char *key;
	char *secret;
};

struct balance {
	struct currency *currency;
	double balance;
	double available;
	double pending;
	char *cryptoaddress;
	int requested;
};

struct user_order {
	char *orderuuid;
	struct market *market;

	double quantity;
	double quantityremaining;
	double limit;
	double commission;
	double price;
	double priceperunit;
	double reserved;
	double reservedremaining;
	double commissionreserved;
	double commissionRR;
	int isopen;
	int isconditional;
	int immediateorcancel;
	int cancelinitiaded;

	char *timestamp;
	char *ordertype;
	char *dateclosed;
	char *condition;
	char *conditiontarget;
};

struct deposit {
	int paymentuid;
	struct currency *currency;
	double amount;
	char *address;
	char *timestamp;
	char *txid;
};

struct api *new_api(char *apik, char *s);
struct balance *getbalance(struct bittrex_info *bi, struct currency *c, struct api *api);
struct balance **getbalances(struct bittrex_info *bi, struct api *api);
struct deposit **getdeposithistory(struct bittrex_info *bi, struct currency *c);
struct user_order *new_user_order();
struct user_order *getorder(struct bittrex_info *bi, char *uuid);
struct user_order **getorderhistory(struct bittrex_info *bi, struct market *m);
struct user_order **getopenorders(struct bittrex_info *bi, struct market *m);
char *getdepositaddress(struct bittrex_info *bi, struct currency *c, struct api *apikey);
int withdraw(struct bittrex_info *bi, struct currency *c, double quantity, char *destaddress, char *paymentid);
int cancel(struct bittrex_info *bi, char *uuid);
char *buylimit(struct bittrex_info *bi, struct market *m, double quantity, double rate);
char *selllimit(struct bittrex_info *bi, struct market *m, double quantity, double rate);
void getwithdrawalhistory(struct bittrex_info *bi, struct currency *c);

void free_user_order(struct user_order *o);
void free_user_orders(struct user_order **orders);
void free_api(struct api *a);
void free_deposit(struct deposit *deposit);
void free_deposits(struct deposit **deposits);
void free_balance(struct balance *b);
void free_balances(struct balance **b);

void printbalance(struct balance *b);
void printbalances(struct balance **b);
void printdeposithistory(struct deposit **dep);
void printorder(struct user_order *o);
void printorders(struct user_order **o);

#endif
