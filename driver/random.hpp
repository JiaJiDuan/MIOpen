#ifndef GUARD_RANDOM_GEN_
#define GUARD_RANDOM_GEN_

#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <random>
#include <chrono>

template <typename T>
inline T FRAND()
{
    std::minstd_rand minstd_gen(std::chrono::system_clock::now().time_since_epoch().count());
    auto d = std::generate_canonical<double, 5>(minstd_gen);
    return static_cast<T>(d);
}

inline int GET_RAND()
{
    std::minstd_rand minstd_gen(std::chrono::system_clock::now().time_since_epoch().count());
    return minstd_gen();
}

template <typename T>
inline T RAN_GEN(T A, T B)
{
    T r = (FRAND<T>() * (B - A)) + A;
    return r;
}

#endif // GUARD_RANDOM_GEN_
