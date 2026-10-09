/*
	openbv: bv2.db without SQLite.

	BV2 keeps a few settings in bv2.db, a 3 KB SQLite database the launcher maintained:
	LauncherSettings (Name, Value: Version, DBVersion, AccountURL, DidSurvey), MasterServers (Score,
	ID, IP, Location, Port) and, on servers, BadChecksum (IP, Name). The game runs eight fixed
	statements against it (CMaster.cpp, GameVar.cpp, Scene.cpp, Server.cpp, ServerRecv.cpp). This
	reads the tables from the shipped bv2.db (the SQLite file format's table b-trees) and answers those
	statements; changes are kept and saved to storage as "bv2db".

	sqlite3_get_table's result is SQLite's: the column names, then the rows, row-major; the game indexes
	it the same way.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <string>
#include <vector>
#include "gasm.h"

extern "C" void *openbv_read_file(const char *path, size_t *size);

namespace
{
	typedef std::vector<std::string> Row;
	struct Table
	{
		std::string name;
		Row columns;
		std::vector<Row> rows;
	};

	// Constructed on first use: GameVar's constructor (a global) reads bv2.db before this file's
	// globals would be initialised.
	std::vector<Table> &tablesRef()
	{
		static std::vector<Table> *t = new std::vector<Table>();
		return *t;
	}
	#define tables tablesRef()
	bool loaded = false;

	Table *table(const char *name)
	{
		for (size_t i = 0; i < tables.size(); i++)
			if (strcasecmp(tables[i].name.c_str(), name) == 0) return &tables[i];
		return 0;
	}

	// --- the SQLite file format, as much as reading small tables needs

	struct File
	{
		const unsigned char *data;
		size_t size;
		unsigned pageSize;
	};

	unsigned be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
	unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

	long long varint(const unsigned char *p, int *len)
	{
		long long v = 0;
		for (int i = 0; i < 9; i++)
		{
			if (i == 8) { v = (v << 8) | p[i]; *len = 9; return v; }
			v = (v << 7) | (p[i] & 0x7f);
			if (!(p[i] & 0x80)) { *len = i + 1; return v; }
		}
		return v;
	}

	// One record (the cell payload) as text values, the way sqlite3_get_table prints them.
	Row record(const unsigned char *p, size_t len, long long rowid)
	{
		Row out;
		int n;
		long long headerLen = varint(p, &n);
		const unsigned char *h = p + n, *hend = p + headerLen, *body = p + headerLen;
		while (h < hend)
		{
			long long type = varint(h, &n);
			h += n;
			char buf[64];
			if (type == 0) { out.push_back(""); continue; }   // NULL (an INTEGER PRIMARY KEY is the rowid)
			if (type >= 1 && type <= 6)
			{
				static const int sizes[] = {0, 1, 2, 3, 4, 6, 8};
				int sz = sizes[type];
				long long v = (body[0] & 0x80) ? -1 : 0;
				for (int i = 0; i < sz; i++) v = (v << 8) | body[i];
				body += sz;
				snprintf(buf, sizeof(buf), "%lld", v);
				out.push_back(buf);
			}
			else if (type == 7)
			{
				unsigned long long bits = 0;
				for (int i = 0; i < 8; i++) bits = (bits << 8) | body[i];
				body += 8;
				double d;
				memcpy(&d, &bits, 8);
				snprintf(buf, sizeof(buf), "%.15g", d);
				out.push_back(buf);
			}
			else if (type == 8) out.push_back("0");
			else if (type == 9) out.push_back("1");
			else if (type >= 12)
			{
				size_t sz = (size_t)((type - (type & 1 ? 13 : 12)) / 2);
				out.push_back(std::string((const char *)body, sz));
				body += sz;
			}
		}
		(void)len; (void)rowid;
		return out;
	}

	void walk(const File &f, unsigned page, std::vector<std::pair<long long, Row> > &rows, int depth)
	{
		if (page == 0 || depth > 8 || (size_t)page * f.pageSize > f.size) return;
		const unsigned char *pg = f.data + (size_t)(page - 1) * f.pageSize;
		const unsigned char *hdr = pg + (page == 1 ? 100 : 0);
		unsigned kind = hdr[0], cells = be16(hdr + 3);
		const unsigned char *ptrs = hdr + (kind == 0x05 ? 12 : 8);
		for (unsigned i = 0; i < cells; i++)
		{
			const unsigned char *cell = pg + be16(ptrs + 2 * i);
			int n;
			if (kind == 0x05)
			{
				walk(f, be32(cell), rows, depth + 1);
			}
			else if (kind == 0x0d)
			{
				long long payload = varint(cell, &n); cell += n;
				long long rowid = varint(cell, &n); cell += n;
				// small tables only: payloads that overflow onto other pages are not followed
				if (payload > (long long)f.pageSize - 35) continue;
				rows.push_back(std::make_pair(rowid, record(cell, (size_t)payload, rowid)));
			}
		}
		if (kind == 0x05) walk(f, be32(hdr + 8), rows, depth + 1);
	}

	// "CREATE TABLE MasterServers (Score NUMERIC, ID INTEGER PRIMARY KEY, IP TEXT, ...)" -> names, and
	// which column is the rowid alias.
	Row columnsOf(const std::string &sql, int *rowidColumn)
	{
		Row cols;
		*rowidColumn = -1;
		size_t open = sql.find('('), close = sql.rfind(')');
		if (open == std::string::npos || close == std::string::npos) return cols;
		std::string body = sql.substr(open + 1, close - open - 1);
		size_t start = 0;
		while (start <= body.size())
		{
			size_t comma = body.find(',', start);
			std::string def = body.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
			size_t a = def.find_first_not_of(" \t\r\n");
			if (a != std::string::npos)
			{
				size_t b = def.find_first_of(" \t\r\n", a);
				cols.push_back(def.substr(a, b == std::string::npos ? std::string::npos : b - a));
				std::string upper = def;
				for (size_t k = 0; k < upper.size(); k++) upper[k] = (char)toupper((unsigned char)upper[k]);
				if (upper.find("INTEGER PRIMARY KEY") != std::string::npos) *rowidColumn = (int)cols.size() - 1;
			}
			if (comma == std::string::npos) break;
			start = comma + 1;
		}
		return cols;
	}

	void loadFile()
	{
		size_t size = 0;
		unsigned char *data = (unsigned char *)openbv_read_file("bv2.db", &size);
		if (!data || size < 512 || memcmp(data, "SQLite format 3", 15) != 0) { free(data); return; }
		File f = {data, size, be16(data + 16)};
		if (f.pageSize == 1) f.pageSize = 65536;
		std::vector<std::pair<long long, Row> > master;
		walk(f, 1, master, 0);
		for (size_t i = 0; i < master.size(); i++)
		{
			const Row &m = master[i].second;   // type, name, tbl_name, rootpage, sql
			if (m.size() < 5 || m[0] != "table") continue;
			Table t;
			t.name = m[1];
			int rowidColumn;
			t.columns = columnsOf(m[4], &rowidColumn);
			std::vector<std::pair<long long, Row> > rows;
			walk(f, (unsigned)atoi(m[3].c_str()), rows, 0);
			for (size_t r = 0; r < rows.size(); r++)
			{
				Row row = rows[r].second;
				row.resize(t.columns.size());
				if (rowidColumn >= 0)
				{
					char buf[32];
					snprintf(buf, sizeof(buf), "%lld", rows[r].first);
					row[rowidColumn] = buf;
				}
				t.rows.push_back(row);
			}
			tables.push_back(t);
		}
		free(data);
	}

	// --- what the game changed, saved to storage

	void save()
	{
		std::string out;
		Table *ls = table("LauncherSettings");
		if (ls)
			for (size_t i = 0; i < ls->rows.size(); i++)
				out += "LauncherSettings\t" + ls->rows[i][0] + "\t" + ls->rows[i][1] + "\n";
		Table *bc = table("BadChecksum");
		if (bc)
			for (size_t i = 0; i < bc->rows.size(); i++)
				out += "BadChecksum\t" + bc->rows[i][0] + "\t" + bc->rows[i][1] + "\n";
		FILE *f = fopen("bv2db.saved", "wb");
		if (f) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
	}

	void setSetting(const std::string &name, const std::string &value);
	void addBadChecksum(const std::string &ip, const std::string &name);

	void loadSaved()
	{
		size_t size = 0;
		char *data = (char *)openbv_read_file("bv2db.saved", &size);
		if (!data) return;
		std::string all(data, size);
		free(data);
		size_t pos = 0;
		while (pos < all.size())
		{
			size_t nl = all.find('\n', pos);
			std::string line = all.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
			pos = nl == std::string::npos ? all.size() : nl + 1;
			size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
			if (t2 == std::string::npos) continue;
			std::string kind = line.substr(0, t1), a = line.substr(t1 + 1, t2 - t1 - 1), b = line.substr(t2 + 1);
			if (kind == "LauncherSettings") setSetting(a, b);
			else if (kind == "BadChecksum") addBadChecksum(a, b);
		}
	}

	void ensureLoaded()
	{
		if (loaded) return;
		loaded = true;
		loadFile();
		if (!table("BadChecksum"))
		{
			Table t;
			t.name = "BadChecksum";
			t.columns.push_back("IP");
			t.columns.push_back("Name");
			tables.push_back(t);
		}
		loadSaved();
	}

	void setSetting(const std::string &name, const std::string &value)
	{
		Table *ls = table("LauncherSettings");
		if (!ls) return;
		for (size_t i = 0; i < ls->rows.size(); i++)
			if (ls->rows[i].size() >= 2 && ls->rows[i][0] == name) { ls->rows[i][1] = value; return; }
		// SQLite's UPDATE changes nothing when no row matches; keep that.
	}

	void addBadChecksum(const std::string &ip, const std::string &name)
	{
		Table *bc = table("BadChecksum");
		Row r;
		r.push_back(ip);
		r.push_back(name);
		bc->rows.push_back(r);
	}

	// The master server bv2.db lists (RndLabs', 78.46.36.43) is gone. The game reaches the one given
	// as the launch parameter master=host:port instead (the game port; the table holds it plus 1000,
	// see CMaster::GetMasterInfos), or none: an empty address, which baboNet refuses at once.
	std::vector<Row> masterServers(const Table &t)
	{
		char value[256] = "";
		gasm_param_str("master", value, sizeof(value));
		std::string host = value;
		int port = 0;
		size_t colon = host.rfind(':');
		if (colon != std::string::npos) { port = atoi(host.c_str() + colon + 1); host = host.substr(0, colon); }
		Row r(t.columns.size());
		for (size_t c = 0; c < t.columns.size(); c++)
		{
			const std::string &n = t.columns[c];
			char buf[32];
			if (strcasecmp(n.c_str(), "Score") == 0 || strcasecmp(n.c_str(), "ID") == 0) r[c] = "1";
			else if (strcasecmp(n.c_str(), "IP") == 0) r[c] = host;
			else if (strcasecmp(n.c_str(), "Port") == 0) { snprintf(buf, sizeof(buf), "%d", port + 1000); r[c] = buf; }
		}
		return std::vector<Row>(1, r);
	}

	// --- the statements

	// Quoted values in order: "... Set Value = 'a' Where Name = 'b'" -> a, b
	std::vector<std::string> quoted(const char *sql)
	{
		std::vector<std::string> out;
		const char *p = sql;
		while ((p = strchr(p, '\'')) != 0)
		{
			const char *e = strchr(p + 1, '\'');
			if (!e) break;
			out.push_back(std::string(p + 1, e - p - 1));
			p = e + 1;
		}
		return out;
	}

	bool startsWith(const char *s, const char *prefix) { return strncasecmp(s, prefix, strlen(prefix)) == 0; }

	char **makeResult(const Row &columns, const std::vector<Row> &rows)
	{
		// As SQLite does: the cell count sits in the slot before the array sqlite3_free_table gets.
		size_t n = columns.size() * (rows.size() + 1);
		char **base = (char **)malloc(sizeof(char *) * (n + 1));
		base[0] = (char *)(size_t)n;
		char **res = base + 1;
		size_t k = 0;
		for (size_t c = 0; c < columns.size(); c++) res[k++] = strdup(columns[c].c_str());
		for (size_t r = 0; r < rows.size(); r++)
			for (size_t c = 0; c < columns.size(); c++)
				res[k++] = c < rows[r].size() ? strdup(rows[r][c].c_str()) : 0;
		return res;
	}
}

struct sqlite3 { int unused; };

extern "C" int sqlite3_open(const char *filename, sqlite3 **ppDb)
{
	(void)filename;
	ensureLoaded();
	if (!table("LauncherSettings")) { *ppDb = 0; return SQLITE_CANTOPEN; }
	static sqlite3 db;
	*ppDb = &db;
	return SQLITE_OK;
}

extern "C" int sqlite3_close(sqlite3 *db) { (void)db; return SQLITE_OK; }

extern "C" int sqlite3_exec(sqlite3 *db, const char *sql, sqlite3_callback callback, void *arg, char **errmsg)
{
	(void)callback; (void)arg;
	if (errmsg) *errmsg = 0;
	if (!db) return SQLITE_ERROR;
	std::vector<std::string> q = quoted(sql);
	if (startsWith(sql, "Update LauncherSettings") && q.size() == 2) { setSetting(q[1], q[0]); save(); return SQLITE_OK; }
	if (startsWith(sql, "Insert into BadChecksum") && q.size() == 2) { addBadChecksum(q[0], q[1]); save(); return SQLITE_OK; }
	return SQLITE_ERROR;
}

extern "C" int sqlite3_get_table(sqlite3 *db, const char *sql, char ***pazResult, int *pnRow, int *pnColumn, char **pzErrmsg)
{
	if (pzErrmsg) *pzErrmsg = 0;
	*pazResult = 0; *pnRow = 0; *pnColumn = 0;
	if (!db) return SQLITE_ERROR;
	Row columns;
	std::vector<Row> rows;
	if (startsWith(sql, "Select Value From LauncherSettings"))
	{
		std::vector<std::string> q = quoted(sql);
		Table *ls = table("LauncherSettings");
		columns.push_back("Value");
		for (size_t i = 0; ls && q.size() == 1 && i < ls->rows.size(); i++)
			if (ls->rows[i][0] == q[0]) rows.push_back(Row(1, ls->rows[i][1]));
	}
	else if (startsWith(sql, "Select * From "))
	{
		char name[64] = "";
		sscanf(sql + 14, "%63[A-Za-z0-9_]", name);
		Table *t = table(name);
		if (!t) return SQLITE_ERROR;
		columns = t->columns;
		rows = t->rows;
		if (strcasecmp(name, "MasterServers") == 0) rows = masterServers(*t);
	}
	else if (startsWith(sql, "Select IP, Name From BadChecksum limit"))
	{
		// "limit N offset (select count(*) from BadChecksum) - M": the last M rows, at most N
		Table *bc = table("BadChecksum");
		int limit = 0, back = 0;
		const char *l = strcasestr(sql, "limit");
		const char *m = strrchr(sql, '-');
		if (l) limit = atoi(l + 5);
		if (m) back = atoi(m + 1);
		int offset = (int)bc->rows.size() - back;
		if (offset < 0) offset = 0;   // SQLite treats a negative offset as none
		columns.push_back("IP");
		columns.push_back("Name");
		for (int i = offset; i < (int)bc->rows.size() && (int)rows.size() < limit; i++) rows.push_back(bc->rows[i]);
	}
	else if (startsWith(sql, "select count(*) as Number from BadChecksum"))
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%d", (int)table("BadChecksum")->rows.size());
		columns.push_back("Number");
		rows.push_back(Row(1, buf));
	}
	else return SQLITE_ERROR;
	*pazResult = makeResult(columns, rows);
	*pnRow = (int)rows.size();
	*pnColumn = (int)columns.size();
	return SQLITE_OK;
}

extern "C" void sqlite3_free_table(char **result)
{
	if (!result) return;
	char **base = result - 1;
	size_t n = (size_t)base[0];
	for (size_t i = 0; i < n; i++) free(result[i]);
	free(base);
}

extern "C" void sqlite3_free(void *p) { free(p); }
