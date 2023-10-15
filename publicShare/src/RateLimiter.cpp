#include "RateLimiter.h"
#include <unistd.h>
#include <sys/time.h>

/* // test code
int test()
{
    RateLimiter r(50);
    r.pass();
    return 0;
} */

RateLimiter::RateLimiter(int qps)
{
    supplyUnitTime = 1000000000L / qps;
    bucketSize = 50;
    tokenLeft = 0;
}

long RateLimiter::now()
{
    struct timeval tv;
    long seconds;

    gettimeofday(&tv, 0);

    seconds = tv.tv_sec;

    return seconds * 1000000000L + tv.tv_usec * 1000L;
}

void RateLimiter::supplyTokens()
{
    long cur = now();

    if (cur - lastAddTokenTime < supplyUnitTime)
    {
        return;
    }

    long newTokens = (cur - lastAddTokenTime) / supplyUnitTime;

    if (newTokens <= 0)
    {
        return;
    }

    mtx.lock();

    // 更新补充时间
    lastAddTokenTime += (newTokens * supplyUnitTime);

    int freeRoom = bucketSize - tokenLeft;

    if (newTokens > freeRoom || freeRoom > bucketSize)
    {
        newTokens = freeRoom > bucketSize ? bucketSize : freeRoom;
    }

    tokenLeft += newTokens;

    mtx.unlock();
}

bool RateLimiter::tryGetToken()
{
    int token;

    supplyTokens();

    mtx.lock();

    token = tokenLeft;

    tokenLeft--;

    mtx.unlock();

    if (token <= 0)
    {
        mtx.lock();
        tokenLeft++;
        mtx.unlock();

        return false;
    }

    return true;
}

void RateLimiter::mustGetToken()
{
    bool isGetToken = false;

    for (int i = 0; i < RETRY_IMEDIATELY_TIMES; i++)
    {
        isGetToken = tryGetToken();

        if (isGetToken)
        {
            return;
        }
    }

    while (1)
    {
        isGetToken = tryGetToken();

        if (isGetToken)
        {
            return;
        }
        else
        {
            sleep(0);
        }
    }
}

void RateLimiter::pass()
{
    return mustGetToken();
}
