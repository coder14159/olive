#ifndef IPC_TIMER_H
#define IPC_TIMER_H

#include "Chrono.h"
#include "TimeDuration.h"

namespace spmc {

/*
 * A stopwatch timer class
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

private:
    TimePoint m_begin;
    TimePoint m_end;
};

} // namespace spmc

#endif // IPC_TIMER_H

