class L:
    def error(self, m, exc_info=False): print("LOG ERROR", m); import traceback; exc_info and traceback.print_exc()
    debugWarning = error
log = L()
