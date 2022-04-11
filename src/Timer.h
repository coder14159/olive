#ifndef OLIVE_TIMER_H
#define OLIVE_TIMER_H

#include "Chrono.h"
#include "TimeDuration.h"

namespace olive {

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

} // namespace olive

#endif // OLIVE_TIMER_H

