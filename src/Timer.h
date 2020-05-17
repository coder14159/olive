#ifndef IPC_TIMER_H
#define IPC_TIMER_H

#include "Time.h"
#include "TimeDuration.h"

#include <chrono>

namespace spmc {

/*
 * Timer class
 */
class Timer
{
public:
    Timer ();

    void start ();

    void stop ();

    Timer &reset ();

    TimeDuration elapsed () const;

private:

    bool is_stopped () const;

    bool is_valid_interval () const;

private:
    TimePoint m_begin;
    TimePoint m_end;
};

} // namespace spmc

#endif // IPC_TIMER_H

