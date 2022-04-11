
namespace olive {

inline
bool LatencyStats::is_stopped () const
{
  return (m_interval.is_stopped () && m_summary.is_stopped ());
}

} // namespace olive
