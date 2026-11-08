import sys
class _Log:
    def _p(self, lvl, msg, exc_info=False):
        print("LOG %s: %s" % (lvl, msg), flush=True)
        if exc_info:
            import traceback; traceback.print_exc()
    def error(self, m, exc_info=False): self._p("ERROR", m, exc_info)
    def debugWarning(self, m, exc_info=False): self._p("DEBUGWARN", m, exc_info)
    def warning(self, m, exc_info=False): self._p("WARN", m, exc_info)
    def debug(self, m, exc_info=False): pass
    def info(self, m, exc_info=False): self._p("INFO", m)
log = _Log()
