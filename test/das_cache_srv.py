#!/usr/bin/env python3
"""Throwaway HTTP file server for test/das_cache_test.sh.

Serves the files in one directory the way a well behaved static server
does: ETag and Last-Modified on every 200, and 304 Not Modified when the
request's If-None-Match matches or its If-Modified-Since is not older than
the file.  With --no-validators it behaves like a server that knows nothing
of caching, which is what dasflex does today.  Every request is appended
to the log file as one line, so the shell test can see what the client
sent and what it got back.

Drafted by Claude Fable 5.1, filed by Chris Piker.  Python 3.6 compatible.
"""

import argparse
import email.utils
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

g_dir = "."
g_log = None
g_validators = True


class Handler(BaseHTTPRequestHandler):

	def log_message(self, fmt, *args):
		pass   # the request log below is the one the test reads

	def _log(self, code, inm, ims):
		with open(g_log, "a") as f:
			f.write("GET %s %d inm=%s ims=%s\n" % (
				self.path, code, inm if inm else "-", ims if ims else "-"
			))

	def do_GET(self):
		sPath = os.path.join(g_dir, self.path.lstrip("/").split("?")[0])
		inm = self.headers.get("If-None-Match")
		ims = self.headers.get("If-Modified-Since")

		if not os.path.isfile(sPath):
			self.send_response(404)
			self.end_headers()
			self._log(404, inm, ims)
			return

		st = os.stat(sPath)
		nMod = int(st.st_mtime)
		sETag = '"%x-%x"' % (int(st.st_mtime * 1000), st.st_size)
		sLastMod = email.utils.formatdate(nMod, usegmt=True)

		if g_validators:
			bCurrent = False
			if inm is not None:
				bCurrent = (inm == sETag)
			elif ims is not None:
				try:
					bCurrent = email.utils.parsedate_to_datetime(ims).timestamp() >= nMod
				except (TypeError, ValueError):
					bCurrent = False
			if bCurrent:
				self.send_response(304)
				self.send_header("ETag", sETag)
				self.send_header("Last-Modified", sLastMod)
				self.end_headers()
				self._log(304, inm, ims)
				return

		with open(sPath, "rb") as f:
			xBody = f.read()
		self.send_response(200)
		self.send_header("Content-Type", "application/json")
		self.send_header("Content-Length", str(len(xBody)))
		if g_validators:
			self.send_header("ETag", sETag)
			self.send_header("Last-Modified", sLastMod)
		self.end_headers()
		self.wfile.write(xBody)
		self._log(200, inm, ims)


def main():
	global g_dir, g_log, g_validators
	psr = argparse.ArgumentParser(description=__doc__.split("\n")[0])
	psr.add_argument("--dir", required=True, help="directory to serve")
	psr.add_argument("--log", required=True, help="request log, appended")
	psr.add_argument("--port-file", required=True, help="the chosen port is written here")
	psr.add_argument("--no-validators", action="store_true", help="send no ETag or Last-Modified")
	opts = psr.parse_args()

	g_dir = opts.dir
	g_log = opts.log
	g_validators = not opts.no_validators

	srv = HTTPServer(("127.0.0.1", 0), Handler)
	with open(opts.port_file, "w") as f:
		f.write("%d\n" % srv.server_port)
	try:
		srv.serve_forever()
	except KeyboardInterrupt:
		pass
	return 0


if __name__ == "__main__":
	sys.exit(main())
