                             

static const char *md_getline(const char *p, const char *end, size_t *len)
{
    const char *e = p;
    while (e < end && *e != '\n')
        e++;
    size_t l = (size_t)(e - p);
    if (l && p[l - 1] == '\r')
        l--;
    *len = l;
    return e < end ? e + 1 : end;
}

static size_t md_indent(const char *s, size_t n, size_t *off)
{
    size_t col = 0, i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
    {
        col += s[i] == '\t' ? 4 : 1;
        i++;
    }
    *off = i;
    return col;
}

static bool md_blank(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!isspace((unsigned char)s[i]))
            return false;
    return true;
}

                                                                     
static int md_peek_indent(const char *p, const char *end)
{
    while (p < end)
    {
        size_t n;
        const char *nx = md_getline(p, end, &n);
        if (!md_blank(p, n))
        {
            size_t off;
            return (int)md_indent(p, n, &off);
        }
        p = nx;
    }
    return -1;
}

static bool md_is_hr(const char *s, size_t n)
{
    size_t off;
    size_t col = md_indent(s, n, &off);
    if (col >= 4 || off >= n)
        return false;
    char c = s[off];
    if (c != '-' && c != '*' && c != '_')
        return false;
    int cnt = 0;
    for (size_t i = off; i < n; i++)
    {
        if (s[i] == c)
            cnt++;
        else if (s[i] != ' ' && s[i] != '\t')
            return false;
    }
    return cnt >= 3;
}

static bool md_is_atx(const char *s, size_t n, int *level, size_t *ts,
                      size_t *te)
{
    size_t off;
    size_t col = md_indent(s, n, &off);
    if (col >= 4)
        return false;
    size_t i = off;
    int l = 0;
    while (i < n && s[i] == '#' && l < 7)
    {
        l++;
        i++;
    }
    if (l < 1 || l > 6)
        return false;
    if (i < n && s[i] != ' ' && s[i] != '\t')
        return false;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    size_t e = n;
    while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t'))
        e--;
    size_t e2 = e;
    while (e2 > i && s[e2 - 1] == '#')
        e2--;
    if (e2 < e && (e2 == i || s[e2 - 1] == ' ' || s[e2 - 1] == '\t'))
    {
        e = e2;
        while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t'))
            e--;
    }
    *level = l;
    *ts = i;
    *te = e;
    return true;
}

static bool md_is_fence(const char *s, size_t n, char *ch, size_t *cnt,
                        const char **info, size_t *infon)
{
    size_t off;
    size_t col = md_indent(s, n, &off);
    if (col >= 4 || off >= n)
        return false;
    char c = s[off];
    if (c != '`' && c != '~')
        return false;
    size_t i = off;
    while (i < n && s[i] == c)
        i++;
    if (i - off < 3)
        return false;
    size_t a = i;
    while (a < n && (s[a] == ' ' || s[a] == '\t'))
        a++;
    size_t e = n;
    while (e > a && (s[e - 1] == ' ' || s[e - 1] == '\t'))
        e--;
    if (c == '`' && memchr(s + a, '`', e - a))
        return false;
    *ch = c;
    *cnt = i - off;
    *info = s + a;
    *infon = e - a;
    return true;
}

static bool md_list_marker(const char *s, size_t n, size_t *indent,
                           bool *ordered, int *start, size_t *coff,
                           size_t *ci)
{
    size_t off;
    size_t col = md_indent(s, n, &off);
    if (off >= n)
        return false;
    size_t i = off;
    bool ord = false;
    int num = 0;
    if (s[i] == '-' || s[i] == '*' || s[i] == '+')
        i++;
    else if (isdigit((unsigned char)s[i]))
    {
        int d = 0;
        while (i < n && isdigit((unsigned char)s[i]) && d < 9)
        {
            num = num * 10 + (s[i] - '0');
            i++;
            d++;
        }
        if (i >= n || (s[i] != '.' && s[i] != ')'))
            return false;
        i++;
        ord = true;
    }
    else
        return false;
    if (i < n && s[i] != ' ' && s[i] != '\t')
        return false;

    size_t content;
    if (i >= n)
        content = n;
    else if (s[i] == '\t')
        content = i + 1;
    else
    {
        size_t sp = 0;
        while (i + sp < n && s[i + sp] == ' ')
            sp++;
        content = (sp > 4 || i + sp >= n) ? i + 1 : i + sp;
        if (i + sp >= n)
            content = n;
    }
    *indent = col;
    *ordered = ord;
    *start = num;
    *coff = content;
    *ci = col + (i - off) + (content > i ? content - i : 1);
    return true;
}

static bool md_starts_block(const char *s, size_t n)
{
    size_t off;
    size_t col = md_indent(s, n, &off);
    if (col >= 4)
        return false;
    if (off < n && s[off] == '>')
        return true;
    char ch;
    size_t cnt, infon;
    const char *info;
    int level;
    size_t ts, te;
    if (md_is_fence(s, n, &ch, &cnt, &info, &infon) ||
        md_is_atx(s, n, &level, &ts, &te) || md_is_hr(s, n))
        return true;
    size_t ind, coff, ci;
    bool ord;
    int start;
    if (md_list_marker(s, n, &ind, &ord, &start, &coff, &ci))
    {
        if (coff >= n)
            return false;                                              
        return !ord || start == 1;
    }
    return false;
}

#define MD_MAX_COLS 32

static size_t md_split_row(const char *s, size_t n, MdLine *cells, size_t max)
{
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    size_t e = n;
    while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t'))
        e--;
    if (i < e && s[i] == '|')
        i++;
    if (e > i && s[e - 1] == '|' && !(e >= 2 && s[e - 2] == '\\'))
        e--;
    size_t cnt = 0, st = i;
    for (size_t j = i; j <= e; j++)
    {
        if (j == e || (s[j] == '|' && (j == 0 || s[j - 1] != '\\')))
        {
            if (cnt < max)
            {
                size_t a = st, z = j;
                while (a < z && (s[a] == ' ' || s[a] == '\t'))
                    a++;
                while (z > a && (s[z - 1] == ' ' || s[z - 1] == '\t'))
                    z--;
                cells[cnt].p = s + a;
                cells[cnt].n = z - a;
                cnt++;
            }
            st = j + 1;
        }
    }
    return cnt;
}

static bool md_is_table_delim(const char *s, size_t n)
{
    if (!memchr(s, '|', n))
        return false;
    MdLine cells[MD_MAX_COLS];
    size_t cnt = md_split_row(s, n, cells, MD_MAX_COLS);
    if (cnt == 0)
        return false;
    for (size_t k = 0; k < cnt; k++)
    {
        const char *c = cells[k].p;
        size_t m = cells[k].n;
        if (m == 0)
            return false;
        size_t a = 0, z = m;
        if (c[a] == ':')
            a++;
        if (z > a && c[z - 1] == ':')
            z--;
        if (z <= a)
            return false;
        for (size_t q = a; q < z; q++)
            if (c[q] != '-')
                return false;
    }
    return true;
}

static void md_blocks(MdBuf *b, const char *src, size_t len, int depth,
                      bool tight, bool allow_icode);

static void md_render_list(MdBuf *b, const char **pp, const char *end,
                           int depth)
{
    const char *p = *pp;
    size_t n;
    md_getline(p, end, &n);
    size_t base, coff, ci;
    bool ord;
    int start;
    md_list_marker(p, n, &base, &ord, &start, &coff, &ci);

    MdBuf *items = NULL;
    int count = 0, cap = 0;
    bool loose = false;

    while (p < end)
    {
        size_t ln;
        const char *nx = md_getline(p, end, &ln);
        size_t i2, coff2, ci2;
        bool o2;
        int st2;
        if (md_is_hr(p, ln) ||
            !md_list_marker(p, ln, &i2, &o2, &st2, &coff2, &ci2) ||
            o2 != ord || i2 < base || i2 > base + 3)
            break;

        if (count == cap)
        {
            int nc = cap ? cap * 2 : 8;
            MdBuf *ni = (MdBuf *)realloc(items, (size_t)nc * sizeof(MdBuf));
            if (!ni)
                break;
            items = ni;
            cap = nc;
        }
        MdBuf *it = &items[count++];
        memset(it, 0, sizeof(*it));

        md_put(it, p + coff2, ln - coff2);
        md_putc(it, '\n');
        bool last_nonblank = coff2 < ln;
        p = nx;

        while (p < end)
        {
            size_t ln2;
            nx = md_getline(p, end, &ln2);
            if (md_blank(p, ln2))
            {
                int pk = md_peek_indent(p, end);
                if (pk > (int)base)
                {
                    md_putc(it, '\n');
                    last_nonblank = false;
                    loose = true;
                    p = nx;
                    continue;
                }
                break;
            }
            size_t o3;
            size_t c3 = md_indent(p, ln2, &o3);
            if (c3 > base)
            {
                size_t strip = c3 < ci2 ? c3 : ci2;
                size_t k = 0, cols = 0;
                while (k < ln2 && cols < strip)
                {
                    if (p[k] == ' ')
                        cols++;
                    else if (p[k] == '\t')
                        cols += 4;
                    else
                        break;
                    k++;
                }
                md_put(it, p + k, ln2 - k);
                md_putc(it, '\n');
                last_nonblank = true;
                p = nx;
                continue;
            }
            {
                size_t si, sc, sci;
                bool so;
                int ss;
                if (md_list_marker(p, ln2, &si, &so, &ss, &sc, &sci))
                    break;                                                 
            }
            if (last_nonblank && !md_starts_block(p, ln2))
            {
                md_put(it, p + o3, ln2 - o3);
                md_putc(it, '\n');
                p = nx;
                continue;
            }
            break;
        }

                                                                           
        if (p < end)
        {
            size_t bl;
            md_getline(p, end, &bl);
            if (md_blank(p, bl))
            {
                const char *q = p;
                while (q < end)
                {
                    size_t qn;
                    const char *qx = md_getline(q, end, &qn);
                    if (!md_blank(q, qn))
                        break;
                    q = qx;
                }
                if (q < end)
                {
                    size_t qn;
                    md_getline(q, end, &qn);
                    size_t qi, qc, qci;
                    bool qo;
                    int qs;
                    if (!md_is_hr(q, qn) &&
                        md_list_marker(q, qn, &qi, &qo, &qs, &qc, &qci) &&
                        qo == ord && qi >= base && qi <= base + 3)
                    {
                        loose = true;
                        p = q;
                    }
                }
            }
        }
    }

    md_puts(b, ord ? "<ol" : "<ul");
    if (ord && start != 1)
    {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), " start=\"%d\"", start);
        md_puts(b, tmp);
    }
    md_puts(b, ">\n");
    for (int k = 0; k < count; k++)
    {
        const char *d = items[k].d ? items[k].d : "";
        size_t dl = items[k].len;
        bool task = dl >= 4 && d[0] == '[' &&
                    (d[1] == ' ' || d[1] == 'x' || d[1] == 'X') &&
                    d[2] == ']' && d[3] == ' ';
        if (task)
        {
            md_puts(b, "<li class=\"task\">");
            md_puts(b, d[1] == ' ' ? "<span class=\"box\">&#9744;</span> "
                                   : "<span class=\"box\">&#9745;</span> ");
            d += 4;
            dl -= 4;
        }
        else
            md_puts(b, "<li>");
        md_blocks(b, d, dl, depth + 1, !loose, false);
        md_puts(b, "</li>\n");
        free(items[k].d);
    }
    free(items);
    md_puts(b, ord ? "</ol>\n" : "</ul>\n");
    *pp = p;
}

static void md_blocks(MdBuf *b, const char *src, size_t len, int depth,
                      bool tight, bool allow_icode)
{
    if (depth > 10)
    {
        md_esc(b, src, len);
        return;
    }
    const char *p = src, *end = src + len;
    while (p < end && !b->oom)
    {
        size_t n;
        const char *next = md_getline(p, end, &n);
        const char *s = p;
        if (md_blank(s, n))
        {
            p = next;
            continue;
        }
        size_t off;
        size_t col = md_indent(s, n, &off);

                         
        char fch;
        size_t fcnt, infon;
        const char *info;
        if (md_is_fence(s, n, &fch, &fcnt, &info, &infon))
        {
            md_puts(b, "<pre><code");
            if (infon)
            {
                size_t w = 0;
                while (w < infon && info[w] != ' ' && info[w] != '\t' &&
                       info[w] != '{')
                    w++;
                if (w)
                {
                    md_puts(b, " class=\"language-");
                    md_esc(b, info, w);
                    md_puts(b, "\"");
                }
            }
            md_puts(b, ">");
            p = next;
            while (p < end)
            {
                size_t ln;
                const char *nx = md_getline(p, end, &ln);
                char c2;
                size_t cnt2, in2;
                const char *i2;
                if (md_is_fence(p, ln, &c2, &cnt2, &i2, &in2) && c2 == fch &&
                    cnt2 >= fcnt && in2 == 0)
                {
                    p = nx;
                    break;
                }
                size_t k = 0;
                while (k < ln && k < col && p[k] == ' ')
                    k++;
                md_esc(b, p + k, ln - k);
                md_putc(b, '\n');
                p = nx;
            }
            md_puts(b, "</code></pre>\n");
            continue;
        }

                           
        if (allow_icode && col >= 4)
        {
            md_puts(b, "<pre><code>");
            while (p < end)
            {
                size_t ln;
                const char *nx = md_getline(p, end, &ln);
                if (md_blank(p, ln))
                {
                    if (md_peek_indent(p, end) >= 4)
                    {
                        md_putc(b, '\n');
                        p = nx;
                        continue;
                    }
                    break;
                }
                size_t o2;
                if (md_indent(p, ln, &o2) < 4)
                    break;
                size_t k = 0, cols = 0;
                while (k < ln && cols < 4)
                {
                    if (p[k] == ' ')
                        cols++;
                    else if (p[k] == '\t')
                        cols += 4;
                    else
                        break;
                    k++;
                }
                md_esc(b, p + k, ln - k);
                md_putc(b, '\n');
                p = nx;
            }
            md_puts(b, "</code></pre>\n");
            continue;
        }

                         
        int level;
        size_t ts, te;
        if (md_is_atx(s, n, &level, &ts, &te))
        {
            char tag[8];
            snprintf(tag, sizeof(tag), "h%d", level);
            md_putc(b, '<');
            md_puts(b, tag);
            md_putc(b, '>');
            md_inline(b, s + ts, te - ts, 0, false);
            md_puts(b, "</");
            md_puts(b, tag);
            md_puts(b, ">\n");
            p = next;
            continue;
        }

                             
        if (md_is_hr(s, n))
        {
            md_puts(b, "<hr>\n");
            p = next;
            continue;
        }

                        
        if (col < 4 && s[off] == '>')
        {
            MdBuf inner = {0};
            bool last_blank = false;
            while (p < end)
            {
                size_t ln;
                const char *nx = md_getline(p, end, &ln);
                size_t o2;
                size_t c2 = md_indent(p, ln, &o2);
                if (!md_blank(p, ln) && c2 < 4 && p[o2] == '>')
                {
                    size_t k = o2 + 1;
                    if (k < ln && p[k] == ' ')
                        k++;
                    md_put(&inner, p + k, ln - k);
                    md_putc(&inner, '\n');
                    last_blank = md_blank(p + k, ln - k);
                    p = nx;
                    continue;
                }
                if (!md_blank(p, ln) && !last_blank &&
                    !md_starts_block(p, ln))
                {
                    md_put(&inner, p + o2, ln - o2);
                    md_putc(&inner, '\n');
                    p = nx;
                    continue;
                }
                break;
            }
            md_puts(b, "<blockquote>\n");
            md_blocks(b, inner.d ? inner.d : "", inner.len, depth + 1, false,
                      allow_icode);
            md_puts(b, "</blockquote>\n");
            free(inner.d);
            continue;
        }

                  
        {
            size_t ind, coff, ci;
            bool ord;
            int start;
            if (col < 4 &&
                md_list_marker(s, n, &ind, &ord, &start, &coff, &ci))
            {
                md_render_list(b, &p, end, depth);
                continue;
            }
        }

                   
        if (memchr(s, '|', n) && next < end)
        {
            size_t n2;
            const char *nx2 = md_getline(next, end, &n2);
            if (md_is_table_delim(next, n2))
            {
                MdLine hc[MD_MAX_COLS], dc[MD_MAX_COLS];
                size_t hn = md_split_row(s, n, hc, MD_MAX_COLS);
                size_t dn = md_split_row(next, n2, dc, MD_MAX_COLS);
                if (hn == dn)
                {
                    const char *align[MD_MAX_COLS];
                    for (size_t k = 0; k < dn; k++)
                    {
                        bool l = dc[k].p[0] == ':';
                        bool r = dc[k].p[dc[k].n - 1] == ':';
                        align[k] = (l && r) ? "center"
                                   : r      ? "right"
                                   : l      ? "left"
                                            : NULL;
                    }
                    md_puts(b, "<table>\n<thead><tr>");
                    for (size_t k = 0; k < hn; k++)
                    {
                        md_puts(b, "<th");
                        if (align[k])
                        {
                            md_puts(b, " align=\"");
                            md_puts(b, align[k]);
                            md_puts(b, "\" style=\"text-align:");
                            md_puts(b, align[k]);
                            md_puts(b, "\"");
                        }
                        md_putc(b, '>');
                        md_inline(b, hc[k].p, hc[k].n, 0, false);
                        md_puts(b, "</th>");
                    }
                    md_puts(b, "</tr></thead>\n<tbody>\n");
                    p = nx2;
                    while (p < end)
                    {
                        size_t rn;
                        const char *rx = md_getline(p, end, &rn);
                        if (md_blank(p, rn) || !memchr(p, '|', rn))
                            break;
                        MdLine rc[MD_MAX_COLS];
                        size_t rcnt = md_split_row(p, rn, rc, MD_MAX_COLS);
                        md_puts(b, "<tr>");
                        for (size_t k = 0; k < hn; k++)
                        {
                            md_puts(b, "<td");
                            if (align[k])
                            {
                                md_puts(b, " align=\"");
                                md_puts(b, align[k]);
                                md_puts(b, "\" style=\"text-align:");
                                md_puts(b, align[k]);
                                md_puts(b, "\"");
                            }
                            md_putc(b, '>');
                            if (k < rcnt)
                                md_inline(b, rc[k].p, rc[k].n, 0, false);
                            md_puts(b, "</td>");
                        }
                        md_puts(b, "</tr>\n");
                        p = rx;
                    }
                    md_puts(b, "</tbody>\n</table>\n");
                    continue;
                }
            }
        }

                                           
        {
            MdBuf para = {0};
            int setext = 0;
            while (p < end)
            {
                size_t ln;
                const char *nx = md_getline(p, end, &ln);
                size_t o2;
                md_indent(p, ln, &o2);
                md_put(&para, p + o2, ln - o2);
                md_putc(&para, '\n');
                p = nx;
                if (p >= end)
                    break;
                size_t n3;
                const char *nx3 = md_getline(p, end, &n3);
                if (md_blank(p, n3))
                    break;
                size_t o3;
                size_t c3 = md_indent(p, n3, &o3);
                if (c3 < 4)
                {
                    size_t z = n3;
                    while (z > o3 && (p[z - 1] == ' ' || p[z - 1] == '\t'))
                        z--;
                    if (z > o3)
                    {
                        bool all_eq = true, all_dash = true;
                        for (size_t q = o3; q < z; q++)
                        {
                            if (p[q] != '=')
                                all_eq = false;
                            if (p[q] != '-')
                                all_dash = false;
                        }
                        if (all_eq || all_dash)
                        {
                            setext = all_eq ? 1 : 2;
                            p = nx3;
                            break;
                        }
                    }
                }
                if (md_starts_block(p, n3))
                    break;
            }
            size_t pl = para.len;
            while (pl && (para.d[pl - 1] == '\n' || para.d[pl - 1] == ' ' ||
                          para.d[pl - 1] == '\t'))
                pl--;
            if (setext)
            {
                md_puts(b, setext == 1 ? "<h1>" : "<h2>");
                md_inline(b, para.d, pl, 0, false);
                md_puts(b, setext == 1 ? "</h1>\n" : "</h2>\n");
            }
            else if (tight)
            {
                md_inline(b, para.d, pl, 0, false);
                md_putc(b, '\n');
            }
            else
            {
                md_puts(b, "<p>");
                md_inline(b, para.d, pl, 0, false);
                md_puts(b, "</p>\n");
            }
            free(para.d);
        }
    }
}

static const char *const k_md_css =
    "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, "
    "Helvetica, Arial, sans-serif; background: #1C1C1E; color: #F2F2F7; "
    "margin: 0; padding: 0; line-height: 1.6; font-size: 15px; }\n"
    ".md { padding: 20px 32px 40px 32px; }\n"
    "h1, h2, h3, h4, h5, h6 { color: #F2F2F7; line-height: 1.25; "
    "margin: 1.2em 0 0.5em 0; }\n"
    "h1 { color: #F2F2F7; font-size: 2em; padding-bottom: 0.3em; "
    "border-bottom: 1px solid #38383A; }\n"
    "h2 { color: #F2F2F7; font-size: 1.5em; padding-bottom: 0.3em; "
    "border-bottom: 1px solid #38383A; }\n"
    "h3 { color: #F2F2F7; font-size: 1.25em; }\n"
    "h4 { color: #F2F2F7; font-size: 1.1em; }\n"
    "h5 { color: #F2F2F7; font-size: 1em; }\n"
    "h6 { color: #8E8E93; font-size: 0.9em; }\n"
    "p { margin: 0.6em 0; }\n"
    "strong { color: #FFFFFF; font-weight: bold; }\n"
    "em { font-style: italic; }\n"
    "del { color: #8E8E93; text-decoration: line-through; }\n"
    "code { font-family: 'SF Mono', 'Ubuntu Mono', Menlo, Consolas, monospace; "
    "background: #2C2C2E; color: #FF9F0A; padding: 2px 5px; "
    "border-radius: 4px; }\n"
    "pre { background: #2C2C2E; color: #F2F2F7; padding: 12px 14px; "
    "border-radius: 8px; overflow: auto; margin: 0.8em 0; }\n"
    "pre code { background: transparent; color: inherit; padding: 0; }\n"
    "blockquote { border-left: 4px solid #0A84FF; margin: 0.8em 0; "
    "padding: 0 0 0 14px; color: #C7C7CC; }\n"
    "a { color: #0A84FF; text-decoration: none; }\n"
    "ul, ol { padding-left: 28px; margin: 0.5em 0; }\n"
    "li { margin: 3px 0; }\n"
    "li.task { list-style: none; margin-left: -20px; }\n"
    ".box { color: #FFD60A; }\n"
    "hr { border: none; border-top: 1px solid #38383A; margin: 1.5em 0; }\n"
    "img { max-width: 100%; }\n"
    "table { border-collapse: collapse; margin: 0.8em 0; }\n"
    "th, td { border: 1px solid #38383A; padding: 6px 12px; }\n"
    "th { background: #2C2C2E; color: #F2F2F7; font-weight: bold; }\n";

                                                                           
static char *markdown_to_html(const char *md)
{
    MdBuf b = {0};
    md_puts(&b, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n"
                "<style>\n");
    md_puts(&b, k_md_css);
    char theme_css[2048];
    snprintf(theme_css, sizeof(theme_css),
             "body{background:#%06X;color:#%06X;}"
             "h1,h2,h3,h4,h5,h6{color:#%06X;}"
             "h1,h2{border-color:#%06X;}"
             "h6,del{color:#%06X;}"
             "strong{color:#%06X;}"
             "code{background:#%06X;color:#%06X;}"
             "pre{background:#%06X;color:#%06X;}"
             "blockquote{border-color:#%06X;color:#%06X;}"
             "a{color:#%06X;}"
             "hr{border-color:#%06X;}"
             "th,td{border-color:#%06X;}"
             "th{background:#%06X;color:#%06X;}",
             C_EDITOR & 0xFFFFFFu, C_TEXT & 0xFFFFFFu,
             C_TEXT & 0xFFFFFFu, C_BORDER & 0xFFFFFFu,
             C_TEXT_SOFT & 0xFFFFFFu,
             C_TEXT & 0xFFFFFFu, M3_SURFACE_HIGH & 0xFFFFFFu,
             M3_ORANGE & 0xFFFFFFu, M3_SURFACE_HIGH & 0xFFFFFFu,
             C_TEXT & 0xFFFFFFu, M3_PRIMARY & 0xFFFFFFu,
             C_TEXT_SOFT & 0xFFFFFFu, M3_PRIMARY & 0xFFFFFFu,
             C_BORDER & 0xFFFFFFu, C_BORDER & 0xFFFFFFu,
             M3_SURFACE_HIGH & 0xFFFFFFu, C_TEXT & 0xFFFFFFu);
    md_puts(&b, theme_css);
    md_puts(&b, "</style>\n</head>\n<body>\n<div class=\"md\">\n");
    md_blocks(&b, md ? md : "", md ? strlen(md) : 0, 0, false, true);
    md_puts(&b, "</div>\n</body>\n</html>\n");
    if (b.oom)
    {
        free(b.d);
        return NULL;
    }
    return b.d;
}

static void markdown_preview_create(void)
{
    markdown_preview_destroy();

    if (s_active_file < 0 || s_active_file >= ide_file_count)
        return;

    IdeFile *file = &ide_files[s_active_file];
    if (!strstr(file->name, ".md"))
        return;

    if (s_editor)
        aroma_node_set_hidden(s_editor, true);

    s_markdown_webview = aroma_webview_create(s_root ? s_root : s_editor_bg,
                                              EDITOR_X, EDITOR_Y,
                                              editor_w, editor_h);
    if (s_markdown_webview)
        aroma_node_set_z_index(s_markdown_webview, 6);
    s_md_x = EDITOR_X;
    s_md_y = EDITOR_Y;
    s_md_w = editor_w;
    s_md_h = editor_h;
    if (!s_markdown_webview)
    {
        if (s_editor)
            aroma_node_set_hidden(s_editor, false);
        return;
    }

    char *html = markdown_to_html(file->text ? file->text : "");
    if (!html)
    {
        markdown_preview_destroy();
        if (s_editor)
            aroma_node_set_hidden(s_editor, false);
        return;
    }

    aroma_webview_load_html(s_markdown_webview, html, NULL);
    free(html);

    s_markdown_visible = true;
    toast("Markdown preview opened");
}

static void project_run(void);
static void project_append_build_output(const char *text);
static void project_append_run_output(const char *text);
static void project_draw_output_panel(void);
static void project_show_in_sidebar(void);

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static AromaNode *find_widget(IncenseRegistry *registry, const char *id)
{
    return registry ? IncenseFindWidgetQuiet(registry, id) : NULL;
}

static AromaFont **ui_font_set(void)
{
    if (!s_assets)
        return NULL;
    if (s_setting_font_family == 0 && s_assets->ui_mono[0])
        return s_assets->ui_mono;
    if (s_setting_font_family == 2 && s_assets->ui_serif[0])
        return s_assets->ui_serif;
    return s_assets->ui;
}

static AromaFont *font_ui(int size)
{
    AromaFont *best = NULL;
    int best_delta = 1000;
    AromaFont **fonts = ui_font_set();
    for (int i = 0; i < IDE_UI_FONT_COUNT; i++)
    {
        if (!fonts || !fonts[i])
            continue;
        int delta = ide_ui_font_sizes[i] - size;
        if (delta < 0)
            delta = -delta;
        if (delta < best_delta)
        {
            best = fonts[i];
            best_delta = delta;
        }
    }
                                                                             
    if (!best && s_assets && fonts != s_assets->ui)
    {
        for (int i = 0; i < IDE_UI_FONT_COUNT; i++)
        {
            if (!s_assets->ui[i])
                continue;
            int delta = ide_ui_font_sizes[i] - size;
            if (delta < 0)
                delta = -delta;
            if (delta < best_delta)
            {
                best = s_assets->ui[i];
                best_delta = delta;
            }
        }
    }
    return best;
}

static AromaFont *font_icon(int size)
{
    AromaFont *best = NULL;
    int best_delta = 1000;
    for (int i = 0; i < IDE_ICON_FONT_COUNT; i++)
    {
        if (!s_assets || !s_assets->icons[i])
            continue;
        int delta = ide_icon_font_sizes[i] - size;
        if (delta < 0)
            delta = -delta;
        if (delta < best_delta)
        {
            best = s_assets->icons[i];
            best_delta = delta;
        }
    }
    return best;
}

static AromaFont *font_mono(int size)
{
    AromaFont *best = NULL;
    int best_delta = 1000;
    for (int i = 0; i < IDE_MONO_FONT_COUNT; i++)
    {
        if (!s_assets || !s_assets->mono[i])
            continue;
        int delta = ide_mono_font_sizes[i] - size;
        if (delta < 0)
            delta = -delta;
        if (delta < best_delta)
        {
            best = s_assets->mono[i];
            best_delta = delta;
        }
    }
    return best;
}

static int text_width(AromaFont *font, const char *text)
{
    if (!font || !text)
        return 0;
    return aroma_font_get_line_width(font, text);
}

static void fill(AromaNode *canvas, int x, int y, int width, int height,
                 int radius, uint32_t color)
{
    if (!canvas)
        return;
    aroma_canvas_draw_rect(canvas, x, y, width, height, radius, true, color);
}

static void line(AromaNode *canvas, int x1, int y1, int x2, int y2,
                 uint32_t color, int thickness)
{
    if (!canvas)
        return;
    aroma_canvas_draw_line(canvas, x1, y1, x2, y2, color, thickness);
}

static void text(AromaNode *canvas, const char *value, int x, int y,
                 uint32_t color, AromaFont *font)
{
    if (!canvas || !font || !value || !value[0])
        return;
    aroma_canvas_draw_text(canvas, value, x, y, color, font);
}

static void text_center(AromaNode *canvas, const char *value, int x, int y,
                        int width, int height, uint32_t color, AromaFont *font)
{
    if (!font || !value || !value[0])
        return;
    int width_px = text_width(font, value);
    int height_px = aroma_font_get_line_height(font);
    text(canvas, value, x + (width - width_px) / 2, y + (height - height_px) / 2,
         color, font);
}

static void icon_center(AromaNode *canvas, const char *glyph, int cx, int cy,
                        int size, uint32_t color)
{
    AromaFont *font = font_icon(size);
    if (!font || !glyph)
        return;
    int width = text_width(font, glyph);
    int height = aroma_font_get_line_height(font);
    text(canvas, glyph, cx - width / 2, cy - height / 2, color, font);
}

static void text_fit(AromaNode *canvas, const char *value, int x, int y,
                     int max_width, uint32_t color, AromaFont *font)
{
    if (!canvas || !font || !value)
        return;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", value);
    if (max_width > 0 && text_width(font, buffer) > max_width)
    {
        size_t length = strlen(buffer);
        while (length > 3 &&
               text_width(font, buffer) > max_width)
        {
            length--;
            buffer[length] = '\0';
        }
        if (length > 3 && length + 3 < sizeof(buffer))
        {
            buffer[length] = '.';
            buffer[length + 1] = '.';
            buffer[length + 2] = '.';
            buffer[length + 3] = '\0';
        }
    }
    text(canvas, buffer, x, y, color, font);
}

static const char *base_name(const char *path)
{
    if (!path)
        return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static const char *file_icon(int file_index)
{
    if (file_index < 0 || file_index >= ide_file_count)
        return AROMA_ICON_DESCRIPTION;
    const char *name = ide_files[file_index].name;
    if (strstr(name, ".tsx") || strstr(name, ".jsx"))
        return AROMA_ICON_CODE;
    if (strstr(name, ".ts") || strstr(name, ".js"))
        return AROMA_ICON_CODE;
    if (strstr(name, ".css"))
        return AROMA_ICON_BRUSH;
    if (strstr(name, ".rs"))
        return AROMA_ICON_MEMORY;
    if (strstr(name, ".toml"))
        return AROMA_ICON_SETTINGS;
    if (strstr(name, ".yml") || strstr(name, ".yaml"))
        return AROMA_ICON_TUNE;
    if (strstr(name, ".aroma"))
        return AROMA_ICON_DASHBOARD;
    if (strstr(name, ".json"))
        return AROMA_ICON_DATA_USAGE;
    if (strstr(name, ".md"))
        return AROMA_ICON_DESCRIPTION;
    if (strcmp(name, "Dockerfile") == 0)
        return AROMA_ICON_CODE;
    return AROMA_ICON_DESCRIPTION;
}

static uint32_t file_icon_color(int file_index)
{
    if (file_index < 0 || file_index >= ide_file_count)
        return C_TEXT_DIM;
    const char *name = ide_files[file_index].name;
    if (strstr(name, ".tsx") || strstr(name, ".jsx") || strstr(name, ".ts") ||
        strstr(name, ".js"))
        return C_BLUE;
    if (strstr(name, ".css"))
        return C_PURPLE;
    if (strstr(name, ".rs"))
        return M3_ORANGE;
    if (strstr(name, ".toml"))
        return C_TEXT_DIM;
    if (strstr(name, ".yml") || strstr(name, ".yaml"))
        return C_CYAN;
    if (strstr(name, ".aroma"))
        return C_PRIMARY;
    if (strstr(name, ".json"))
        return C_YELLOW;
    if (strcmp(name, "Dockerfile") == 0)
        return C_BLUE;
    return C_TEXT_DIM;
}

static const char *language_label(AromaCodeLanguage language)
{
    switch (language)
    {
    case AROMA_CODE_LANGUAGE_TSX:
        return "TypeScript JSX";
    case AROMA_CODE_LANGUAGE_TYPESCRIPT:
        return "TypeScript";
    case AROMA_CODE_LANGUAGE_JAVASCRIPT:
        return "JavaScript";
    case AROMA_CODE_LANGUAGE_RUST:
        return "Rust";
    case AROMA_CODE_LANGUAGE_CSS:
        return "CSS";
    case AROMA_CODE_LANGUAGE_YAML:
        return "YAML";
    case AROMA_CODE_LANGUAGE_TOML:
        return "TOML";
    case AROMA_CODE_LANGUAGE_JSON:
        return "JSON";
    case AROMA_CODE_LANGUAGE_DOCKERFILE:
        return "Dockerfile";
    case AROMA_CODE_LANGUAGE_AROMA:
        return "Incense";
    case AROMA_CODE_LANGUAGE_MARKDOWN:
        return "Markdown";
    case AROMA_CODE_LANGUAGE_C:
        return "C";
    case AROMA_CODE_LANGUAGE_CPP:
        return "C++";
    default:
        return "Plain Text";
    }
}

static bool rect_contains(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void log_add(const char *method, const char *format, ...)
{
    char message[200];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    LogEntry *entry;
    if (s_log_count < LOG_CAPACITY)
    {
        entry = &s_logs[s_log_count++];
    }
    else
    {
        memmove(&s_logs[0], &s_logs[1], sizeof(LogEntry) * (LOG_CAPACITY - 1));
        entry = &s_logs[LOG_CAPACITY - 1];
    }
    memset(entry, 0, sizeof(*entry));
    time_t now = time(NULL);
    struct tm *tm_value = localtime(&now);
    if (tm_value)
        strftime(entry->time_text, sizeof(entry->time_text), "%H:%M:%S", tm_value);
    else
        snprintf(entry->time_text, sizeof(entry->time_text), "00:00:00");
    snprintf(entry->method, sizeof(entry->method), "%s", method ? method : "log");
    snprintf(entry->message, sizeof(entry->message), "%s", message);

    if (strstr(entry->method, "error") || strstr(entry->method, "diagnostic"))
        entry->color = C_RED;
    else if (strstr(entry->method, "completion") || strstr(entry->method, "hover"))
        entry->color = C_CYAN;
    else if (strstr(entry->method, "definition"))
        entry->color = C_GREEN;
    else if (strstr(entry->method, "initialize"))
        entry->color = C_GREEN;
    else if (strstr(entry->method, "didChange"))
        entry->color = C_TEXT_DIM;
    else
        entry->color = C_BLUE;
}

static void toast(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    snprintf(s_toast, sizeof(s_toast), "%s", message);
    s_toast_until = now_ms() + 3400u;
    s_toast_visible = true;
    if (s_toast_canvas)
        aroma_node_set_hidden(s_toast_canvas, false);
    draw_toast();
}

static void hide_toast(void)
{
    if (!s_toast_visible)
        return;
    s_toast_visible = false;
    s_toast_until = 0;
    if (s_toast_canvas)
        aroma_node_set_hidden(s_toast_canvas, true);
}

static int tree_row_capacity(void)
{
    int bottom = s_panel_visible ? panel_y : status_y;
    int height = bottom - TREE_TOP - 8;
    int rows = height / TREE_ROW_HEIGHT;
    return rows > 0 ? rows : 1;
}

static void tree_clamp_scroll(void)
{
    int maximum = s_tree_row_count - tree_row_capacity();
    if (maximum < 0)
        maximum = 0;
    if (s_tree_scroll > maximum)
        s_tree_scroll = maximum;
    if (s_tree_scroll < 0)
        s_tree_scroll = 0;
}

static int tree_add_node(const char *name, const char *path, int depth, bool is_dir,
                         int file_index, int parent)
{
    if (s_tree_node_count >= IDE_MAX_TREE_NODES)
        return -1;
    IdeTreeItem *node = &s_tree_nodes[s_tree_node_count];
    memset(node, 0, sizeof(*node));
    snprintf(node->name, sizeof(node->name), "%s", name ? name : "");
    snprintf(node->path, sizeof(node->path), "%s", path ? path : "");
    node->depth = depth;
    node->is_dir = is_dir;
    node->expanded = true;
    node->file_index = file_index;
    node->parent = parent;
    node->first_child = -1;
    node->next_sibling = -1;
    return s_tree_node_count++;
}

static void tree_append_child(int parent, int child)
{
    if (parent < 0 || child < 0 || parent >= s_tree_node_count ||
        child >= s_tree_node_count)
        return;
    IdeTreeItem *node = &s_tree_nodes[parent];
    if (node->first_child < 0)
    {
        node->first_child = child;
        return;
    }
    int current = node->first_child;
    while (s_tree_nodes[current].next_sibling >= 0)
        current = s_tree_nodes[current].next_sibling;
    s_tree_nodes[current].next_sibling = child;
}

static int tree_find_child(int parent, const char *name, bool is_dir)
{
    if (parent < 0 || parent >= s_tree_node_count)
        return -1;
    for (int child = s_tree_nodes[parent].first_child; child >= 0;
         child = s_tree_nodes[child].next_sibling)
    {
        if (s_tree_nodes[child].is_dir == is_dir &&
            strcmp(s_tree_nodes[child].name, name) == 0)
            return child;
    }
    return -1;
}

static int tree_child_compare(const void *left, const void *right)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    if (s_tree_nodes[a].is_dir != s_tree_nodes[b].is_dir)
        return s_tree_nodes[a].is_dir ? -1 : 1;
    return strcasecmp(s_tree_nodes[a].name, s_tree_nodes[b].name);
}

static void tree_sort_children(int parent)
{
    if (parent < 0 || parent >= s_tree_node_count)
        return;
    int count = 0;
    int children[IDE_MAX_TREE_NODES];
    for (int child = s_tree_nodes[parent].first_child; child >= 0;
         child = s_tree_nodes[child].next_sibling)
    {
        if (count < IDE_MAX_TREE_NODES)
            children[count++] = child;
    }
    if (count > 1)
        qsort(children, (size_t)count, sizeof(children[0]), tree_child_compare);
    s_tree_nodes[parent].first_child = count > 0 ? children[0] : -1;
    for (int i = 0; i < count; i++)
    {
        s_tree_nodes[children[i]].next_sibling = -1;
        if (i > 0)
            s_tree_nodes[children[i - 1]].next_sibling = children[i];
        tree_sort_children(children[i]);
    }
}

static void tree_flatten(int node, int *row)
{
    if (node < 0 || node >= s_tree_node_count || *row >= IDE_MAX_TREE_NODES)
        return;
    s_tree_rows[(*row)++] = node;
    if (!s_tree_nodes[node].is_dir || !s_tree_nodes[node].expanded)
        return;
    for (int child = s_tree_nodes[node].first_child; child >= 0;
         child = s_tree_nodes[child].next_sibling)
        tree_flatten(child, row);
}

static void tree_rebuild_rows(void)
{
    int row = 0;
    for (int child = s_tree_nodes[0].first_child; child >= 0;
         child = s_tree_nodes[child].next_sibling)
        tree_flatten(child, &row);
    s_tree_row_count = row;
    tree_clamp_scroll();
}

static void tree_build(void)
{
    s_tree_node_count = 0;
    s_tree_row_count = 0;
    s_tree_scroll = 0;
    int root = tree_add_node(ide_workspace_project_name(), "", -1, true, -1, -1);
    if (root < 0)
        return;

    for (int i = 0; i < ide_file_count; i++)
    {
        char path[IDE_PATH_MAX];
        snprintf(path, sizeof(path), "%s", ide_files[i].path);
        char component[IDE_NAME_MAX];
        int parent = 0;
        const char *cursor = path;
        while (*cursor)
        {
            const char *slash = strchr(cursor, '/');
            size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
            if (length >= sizeof(component))
                length = sizeof(component) - 1;
            memcpy(component, cursor, length);
            component[length] = '\0';
            bool last = slash == NULL;

            if (last)
            {
                if (tree_find_child(parent, component, false) < 0)
                {
                    int node = tree_add_node(component, ide_files[i].path,
                                             s_tree_nodes[parent].depth + 1, false, i,
                                             parent);
                    tree_append_child(parent, node);
                }
                break;
            }

            int existing = tree_find_child(parent, component, true);
            if (existing < 0)
            {
                char partial[IDE_PATH_MAX];
                size_t prefix = (size_t)(cursor - path) + length;
                if (prefix >= sizeof(partial))
                    prefix = sizeof(partial) - 1;
                memcpy(partial, path, prefix);
                partial[prefix] = '\0';
                existing = tree_add_node(component, partial,
                                         s_tree_nodes[parent].depth + 1, true, -1,
                                         parent);
                tree_append_child(parent, existing);
            }
            parent = existing;
            cursor = slash + 1;
        }
    }
    tree_sort_children(0);
    tree_rebuild_rows();
}

static void tree_toggle_all(bool expanded)
{
    for (int i = 0; i < s_tree_node_count; i++)
    {
        if (s_tree_nodes[i].is_dir)
            s_tree_nodes[i].expanded = expanded;
    }
    tree_rebuild_rows();
}

static const char *ci_strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0])
        return haystack;
    size_t needle_length = strlen(needle);
    for (const char *p = haystack; *p; p++)
    {
        size_t i = 0;
        while (i < needle_length && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == needle_length)
            return p;
    }
    return NULL;
}

static void workspace_search(const char *query)
{
    s_search_hit_count = 0;
    s_search_scroll = 0;
    snprintf(s_search_query, sizeof(s_search_query), "%s", query ? query : "");

    if (!s_search_query[0])
    {
        snprintf(s_search_summary, sizeof(s_search_summary),
                 "Type a query to search every file");
        return;
    }

    int files_with_hits = 0;
    for (int file_index = 0; file_index < ide_file_count &&
                             s_search_hit_count < SEARCH_MAX_HITS;
         file_index++)
    {
        const char *text = ide_files[file_index].text ? ide_files[file_index].text : "";
        int line_number = 1;
        int file_hits = 0;
        const char *line = text;
        while (*line && s_search_hit_count < SEARCH_MAX_HITS)
        {
            const char *end = strchr(line, '\n');
            if (!end)
                end = line + strlen(line);
            size_t line_length = (size_t)(end - line);
            char buffer[512];
            size_t copy = line_length < sizeof(buffer) - 1 ? line_length
                                                          : sizeof(buffer) - 1;
            memcpy(buffer, line, copy);
            buffer[copy] = '\0';

            const char *found = ci_strstr(buffer, s_search_query);
            if (found)
            {
                if (file_hits == 0)
                    files_with_hits++;
                file_hits++;
                SearchHit *hit = &s_search_hits[s_search_hit_count++];
                memset(hit, 0, sizeof(*hit));
                hit->file = file_index;
                hit->line = line_number;
                hit->column = (int)(found - buffer) + 1;
                hit->length = (int)strlen(s_search_query);

                const char *snippet = buffer;
                while (*snippet == ' ' || *snippet == '\t')
                    snippet++;
                int offset = (int)(found - buffer);
                if (offset > 56)
                    snippet = found - 56;
                size_t snippet_length = strlen(snippet);
                if (snippet_length >= sizeof(hit->snippet))
                    snippet_length = sizeof(hit->snippet) - 1;
                memcpy(hit->snippet, snippet, snippet_length);
                hit->snippet[snippet_length] = '\0';
                for (size_t i = 0; i < snippet_length; i++)
                {
                    if (hit->snippet[i] == '\n' || hit->snippet[i] == '\r')
                        hit->snippet[i] = ' ';
                }
            }

            if (*end == '\0')
                break;
            line = end + 1;
            line_number++;
        }
    }

    snprintf(s_search_summary, sizeof(s_search_summary), "%d result%s in %d file%s",
             s_search_hit_count, s_search_hit_count == 1 ? "" : "s",
             files_with_hits, files_with_hits == 1 ? "" : "s");
}

static void save_active_state(void)
{
    if (s_active_file < 0 || s_active_file >= ide_file_count || !s_editor)
        return;
    IdeFile *file = &ide_files[s_active_file];
    int scroll_x = 0;
    int scroll_y = 0;
    aroma_code_editor_get_scroll(s_editor, &scroll_x, &scroll_y);
    file->scroll_x = scroll_x;
    file->scroll_y = scroll_y;
    size_t start = 0;
    size_t end = 0;
    if (aroma_code_editor_get_selection(s_editor, &start, &end))
    {
        file->anchor = start;
        file->cursor = end;
        file->has_selection = true;
    }
    else
    {
        file->cursor = aroma_code_editor_get_cursor(s_editor);
        file->anchor = file->cursor;
        file->has_selection = false;
    }
}

static void refresh_outline(void)
{
    s_outline_count = 0;
    if (s_active_file >= 0)
        s_outline_count = ide_lsp_outline(s_active_file, s_outline, MAX_SYMBOLS);
    s_outline_scroll = 0;
    s_outline_hover = -1;
    draw_outline();
}

static void draw_all(void)
{
    draw_title();
    draw_rail();
    draw_sidebar();
    draw_tabs();
    draw_breadcrumb();
    draw_outline();
    designer_draw_all();
    draw_panel();
    draw_status();
    draw_menu();
    draw_toast();

                        
    if (!s_menu_hover)
    {
                                                          
        if (!s_sidebar_detached)
        {
            int hx = SIDEBAR_X + s_sidebar_w;
            fill(s_sidebar_canvas, hx, TITLE_H, 4, win_h - TITLE_H - STATUS_H,
                 0, s_resizing_sidebar ? C_PRIMARY : C_BORDER);
        }
    }
}

static int tab_width(void)
{
    if (s_tab_count <= 0)
        return 190;
    int available = editor_w - TAB_ACTION_W;
    if (available < 1)
        available = 1;
    int width = available / s_tab_count;
    if (width > 190)
        width = 190;
    return width;
}

static bool ensure_tab_capacity(void)
{
    if (s_tab_count < EDITOR_TAB_LIMIT)
        return true;
    int remove = EDITOR_TAB_LIMIT - 1;
    if (s_tabs[remove] == s_active_file)
        remove--;
    if (remove < 0)
        return false;
    if (s_tabs[remove] >= 0 && s_tabs[remove] < ide_file_count)
        ide_files[s_tabs[remove]].open = false;
    for (int i = remove; i < s_tab_count - 1; i++)
        s_tabs[i] = s_tabs[i + 1];
    s_tab_count--;
    return true;
}

static char *normalize_save_text(const char *source, bool trim_trailing,
                                 bool final_newline)
{
    if (!source)
        source = "";
    size_t length = strlen(source);
    char *out = (char *)malloc(length + 2);
    if (!out)
        return NULL;

    size_t written = 0;
    const char *p = source;
    while (*p)
    {
        const char *nl = strchr(p, '\n');
        size_t line_length = nl ? (size_t)(nl - p) : strlen(p);
        bool has_cr = line_length > 0 && p[line_length - 1] == '\r';
        size_t content_length = has_cr ? line_length - 1 : line_length;
        if (trim_trailing)
        {
            while (content_length > 0 &&
                   (p[content_length - 1] == ' ' ||
                    p[content_length - 1] == '\t'))
                content_length--;
        }
        memcpy(out + written, p, content_length);
        written += content_length;
        if (nl)
        {
            if (has_cr)
                out[written++] = '\r';
            out[written++] = '\n';
            p = nl + 1;
        }
        else
        {
            break;
        }
    }
    if (final_newline && (written == 0 || out[written - 1] != '\n'))
        out[written++] = '\n';
    out[written] = '\0';
    return out;
}

static bool save_file_ex(int index, bool quiet)
{
    if (index < 0 || index >= ide_file_count)
        return false;
    if (s_designer_visible && index == s_active_file)
        designer_apply_to_file();
    editor_flush_pending(true);
    IdeFile *file = &ide_files[index];
    if (!file->dirty)
    {
        if (!quiet)
            toast("%s is already saved", file->name);
        return false;
    }

                                                                        
                                                                            
                                         
    char *normalized = normalize_save_text(file->text ? file->text : "",
                                           s_setting_trim_trailing,
                                           s_setting_final_newline);
    if (normalized)
    {
        if (!file->text || strcmp(normalized, file->text) != 0)
        {
            free(file->text);
            file->text = normalized;
        }
        else
        {
            free(normalized);
        }
    }

    if (!ide_workspace_save(index))
    {
        log_add("workspace/applyEdit", "Save failed: %s",
                ide_workspace_last_error());
        if (!quiet)
            toast("Could not save %s", file->name);
        return false;
    }
    log_add("workspace/applyEdit", "Saved %s (%zu bytes)", file->path,
            file->text ? strlen(file->text) : 0u);
    draw_tabs();
    draw_sidebar();
    draw_status();
    if (!quiet)
        toast("Saved %s to disk", file->name);
    return true;
}

static void save_file(int index)
{
    save_file_ex(index, false);
}

static int save_all_files_ex(bool quiet)
{
    if (s_designer_visible)
        designer_apply_to_file();
    int saved = 0;
    for (int i = 0; i < ide_file_count; i++)
    {
        if (ide_files[i].open && ide_files[i].dirty)
        {
            if (save_file_ex(i, true))
                saved++;
        }
    }
    draw_tabs();
    draw_sidebar();
    draw_status();
    if (!quiet)
        toast("Saved %d file%s", saved, saved == 1 ? "" : "s");
    return saved;
}

typedef struct DesignPaletteItem
{
    const char *name;
    const char *label;
    const char *icon;
} DesignPaletteItem;

static const DesignPaletteItem k_design_palette[] = {
                      
    {"Container",        "Container",    AROMA_ICON_WIDGETS},
    {"ScrollView",       "Scroll View",  AROMA_ICON_UNFOLD_MORE},
    {"Card",             "Card",         AROMA_ICON_CROP_SQUARE},
                            
    {"Label",            "Label",        AROMA_ICON_TEXT_FIELDS},
    {"Icon",             "Icon",         AROMA_ICON_INSERT_EMOTICON},
    {"Image",            "Image",        AROMA_ICON_IMAGE},
    {"GIF",              "GIF",          AROMA_ICON_GIF},
    {"Video",            "Video",        AROMA_ICON_VIDEO_LIBRARY},
                     
    {"Button",           "Button",       AROMA_ICON_ADD},
    {"IconButton",       "Icon Button",  AROMA_ICON_TOUCH_APP},
    {"Textbox",          "Text Field",   AROMA_ICON_EDIT},
    {"Switch",           "Switch",       AROMA_ICON_CHECK_BOX},
    {"Checkbox",         "Checkbox",     AROMA_ICON_CHECK_BOX},
    {"RadioButton",      "Radio Button", AROMA_ICON_RADIO_BUTTON_CHECKED},
    {"Slider",           "Slider",       AROMA_ICON_TUNE},
    {"Dropdown",         "Dropdown",     AROMA_ICON_ARROW_DROP_DOWN_CIRCLE},
    {"Chip",             "Chip",         AROMA_ICON_LOCAL_OFFER},
                       
    {"ProgressBar",      "Progress Bar", AROMA_ICON_LINEAR_SCALE},
    {"Gauge",            "Gauge",        AROMA_ICON_SHOW_CHART},
    {"Loading",          "Loading",      AROMA_ICON_AUTORENEW},
    {"Divider",          "Divider",      AROMA_ICON_REMOVE},
    {"Chart",            "Chart",        AROMA_ICON_SHOW_CHART},
    {"Tooltip",          "Tooltip",      AROMA_ICON_INFO_OUTLINE},
                          
    {"Tabs",             "Tabs",         AROMA_ICON_TAB},
    {"SegmentedControl", "Segmented",    AROMA_ICON_FORMAT_ALIGN_CENTER},
    {"Sidebar",          "Sidebar",      AROMA_ICON_MENU},
    {"Menu",             "Menu",         AROMA_ICON_MORE_VERT},
                    
    {"ListView",         "List View",    AROMA_ICON_LIST},
    {"Table",            "Table",        AROMA_ICON_GRID_ON},
                        
    {"Snackbar",         "Snackbar",     AROMA_ICON_ANNOUNCEMENT},
    {"Dialog",           "Dialog",       AROMA_ICON_OPEN_IN_NEW},
                        
    {"Map",              "Map",          AROMA_ICON_MAP},
    {"Canvas",           "Canvas",       AROMA_ICON_BRUSH},
    {"ThreeDViewer",     "3D Viewer",    AROMA_ICON_3D_ROTATION},
};
#define DESIGN_PALETTE_COUNT ((int)(sizeof(k_design_palette) / sizeof(k_design_palette[0])))
#define DESIGN_PALETTE_ITEM_Y (design_body_y + 8)
#define DESIGN_PALETTE_ITEM_H 38
#define DS_TOOL_W 78
#define DS_TOOL_H 26

typedef struct DesignerView
{
    int x;
    int y;
    int width;
    int height;
    int scale;
    int origin_x;
    int origin_y;
} DesignerView;

static bool designer_is_markup_path(const char *path)
{
    if (!path)
        return false;
    size_t length = strlen(path);
    return length >= 6 && strcasecmp(path + length - 6, ".aroma") == 0;
}

static uint32_t designer_color_attr(const IdeDesignDocument *doc, int index,
                                    uint32_t fallback)
{
    const char *value = ide_design_attr(doc, index, "color");
    if (!value || value[0] != '#' || strlen(value) < 7)
        return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(value + 1, &end, 16);
    if (end == value + 1)
        return fallback;
    if (strlen(value) == 7)
        return 0xFF000000u | (uint32_t)parsed;
    if (strlen(value) == 9)
        return (uint32_t)parsed;
    return fallback;
}

static int designer_scaled(int value, int scale)
{
    return (value * scale) >> 8;
}

static int designer_node_font_px(const IdeDesignDocument *doc, int index,
                                 int scale)
{
    int base = 15;
    const char *style = ide_design_attr(doc, index, "style");
    if (style)
    {
        if (strcasecmp(style, "large") == 0)
            base = 24;
        else if (strcasecmp(style, "medium") == 0)
            base = 18;
        else if (strcasecmp(style, "small") == 0)
            base = 11;
    }
    int px = designer_scaled(base, scale);
    if (px < 8)
        px = 8;
    if (px > 26)
        px = 26;
    return px;
}

static int designer_node_radius(const IdeDesignDocument *doc, int index,
                                int scale)
{
    const char *value = ide_design_attr(doc, index, "corner_radius");
    int radius = value ? atoi(value) : 8;
    if (radius < 0)
        radius = 0;
    return designer_scaled(radius, scale);
}

static void designer_canvas_view(DesignerView *view)
{
    view->x = DESIGN_CANVAS_X + 12;
    view->y = design_body_y + 40;
    view->width = design_canvas_w - 24;
    view->height = design_body_h - 56;
    view->scale = 256;
    if (s_design_doc.page_width > 0 && s_design_doc.page_height > 0)
    {
        int sx = (view->width << 8) / s_design_doc.page_width;
        int sy = (view->height << 8) / s_design_doc.page_height;
        view->scale = sx < sy ? sx : sy;
        if (view->scale > 256)
            view->scale = 256;
        if (view->scale < 32)
            view->scale = 32;
    }
    int page_w = designer_scaled(s_design_doc.page_width, view->scale);
    int page_h = designer_scaled(s_design_doc.page_height, view->scale);
    view->origin_x = view->x + (view->width - page_w) / 2;
    view->origin_y = view->y + (view->height - page_h) / 2;
}

static bool designer_view_point(const DesignerView *view, int mouse_x,
                                int mouse_y, int *page_x, int *page_y)
{
    if (!view || view->scale <= 0)
        return false;
    int x = ((mouse_x - view->origin_x) << 8) / view->scale;
    int y = ((mouse_y - view->origin_y) << 8) / view->scale;
    if (x < 0 || y < 0 || x >= s_design_doc.page_width ||
        y >= s_design_doc.page_height)
        return false;
    if (page_x)
        *page_x = x;
    if (page_y)
        *page_y = y;
    return true;
}

static void designer_draw_node(AromaNode *canvas, const DesignerView *view,
                               int index, bool preview)
{
    const IdeDesignNode *node = &s_design_doc.nodes[index];
    int x = view->origin_x + designer_scaled(node->abs_x, view->scale);
    int y = view->origin_y + designer_scaled(node->abs_y, view->scale);
    int w = designer_scaled(node->abs_width, view->scale);
    int h = designer_scaled(node->abs_height, view->scale);
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    int radius = designer_node_radius(&s_design_doc, index, view->scale);
    const char *tag = node->tag;
    const char *text_value = ide_design_attr(&s_design_doc, index, "text");
    uint32_t color = designer_color_attr(&s_design_doc, index, M3_SURFACE_LOW);
    int font_px = designer_node_font_px(&s_design_doc, index, view->scale);
    AromaFont *font = font_ui(font_px);

    if (strcmp(tag, "Card") == 0 || strcmp(tag, "Panel") == 0)
    {
        const char *type = ide_design_attr(&s_design_doc, index, "type");
        if (type && strcasecmp(type, "outlined") == 0)
        {
            fill(canvas, x, y, w, h, radius, 0x6618181Au);
            line(canvas, x, y, x + w, y, C_BORDER_SOFT, 1);
            line(canvas, x, y + h - 1, x + w, y + h - 1, C_BORDER_SOFT, 1);
            line(canvas, x, y, x, y + h, C_BORDER_SOFT, 1);
            line(canvas, x + w - 1, y, x + w - 1, y + h, C_BORDER_SOFT, 1);
        }
        else
        {
            fill(canvas, x, y, w, h, radius, color);
            fill(canvas, x, y, w, 1, 0, M3_SURFACE_HIGH);
        }
    }
    else if (strcmp(tag, "Button") == 0)
    {
        const char *type = ide_design_attr(&s_design_doc, index, "type");
        bool outlined = type && strcasecmp(type, "outlined") == 0;
        if (!outlined)
            fill(canvas, x, y, w, h, radius > 0 ? radius : 6, C_PRIMARY);
        else
            fill(canvas, x, y, w, h, radius > 0 ? radius : 6, 0x22FFFFFFu);
        uint32_t border = outlined ? C_PRIMARY : C_PRIMARY;
        line(canvas, x, y, x + w, y, border, 1);
        line(canvas, x, y + h - 1, x + w, y + h - 1, border, 1);
        line(canvas, x, y, x, y + h, border, 1);
        line(canvas, x + w - 1, y, x + w - 1, y + h, border, 1);
        const char *label = text_value && text_value[0] ? text_value : "Button";
        int tw = text_width(font, label);
        text_fit(canvas, label, x + (w - tw) / 2, y + (h - font_px) / 2,
                 w - 8, M3_ON_PRIMARY, font);
    }
    else if (strcmp(tag, "Label") == 0)
    {
        const char *label = text_value && text_value[0] ? text_value : "Label";
        uint32_t fg = designer_color_attr(&s_design_doc, index, C_TEXT);
        text_fit(canvas, label, x, y, w + 20, fg, font);
    }
    else if (strcmp(tag, "TextBox") == 0 || strcmp(tag, "Textbox") == 0 ||
             strcmp(tag, "Input") == 0)
    {
        fill(canvas, x, y, w, h, radius > 0 ? radius : 5, M3_SURFACE_CONTAINER);
        fill(canvas, x, y, w, 1, 0, C_BORDER_SOFT);
        const char *label = ide_design_attr(&s_design_doc, index, "placeholder");
        if (!label || !label[0])
            label = text_value;
        if (!label || !label[0])
            label = "Type here...";
        text_fit(canvas, label, x + 10, y + (h - font_px) / 2, w - 20,
                 C_TEXT_FAINT, font);
    }
    else if (strcmp(tag, "Image") == 0)
    {
        fill(canvas, x, y, w, h, radius > 0 ? radius : 4, M3_SURFACE_CONTAINER);
        fill(canvas, x, y, w, 1, 0, C_BORDER_SOFT);
        icon_center(canvas, AROMA_ICON_IMAGE, x + w / 2, y + h / 2,
                    font_px + 4, C_TEXT_FAINT);
    }
    else if (strcmp(tag, "Switch") == 0)
    {
        fill(canvas, x, y, w, h, h / 2, M3_SURFACE_HIGH);
        fill(canvas, x + w - h + 2, y + 2, h - 4, h - 4, (h - 4) / 2,
             C_PRIMARY);
    }
    else if (strcmp(tag, "Checkbox") == 0)
    {
        int box = h > 20 ? 18 : h;
        fill(canvas, x, y + (h - box) / 2, box, box, 3, C_PRIMARY);
        icon_center(canvas, AROMA_ICON_CHECK, x + box / 2, y + h / 2,
                    box - 4, M3_ON_PRIMARY);
        if (text_value && text_value[0])
            text_fit(canvas, text_value, x + box + 8, y + (h - font_px) / 2,
                     w - box - 8, C_TEXT, font);
    }
    else if (strcmp(tag, "Slider") == 0 || strcmp(tag, "ProgressBar") == 0)
    {
        fill(canvas, x, y + h / 2 - 2, w, 4, 2, M3_SURFACE_HIGH);
        fill(canvas, x, y + h / 2 - 2, (int)(w * 0.62f), 4, 2, C_PRIMARY);
        fill(canvas, x + (int)(w * 0.62f) - 8, y + h / 2 - 8, 16, 16, 8,
             C_PRIMARY);
    }
    else if (strcmp(tag, "Canvas") == 0)
    {
        if (!preview)
        {
            fill(canvas, x, y, w, h, 2, 0x22FFFFFFu);
            line(canvas, x, y, x + w, y, 0x660A84FFu, 1);
            line(canvas, x, y + h - 1, x + w, y + h - 1, 0x660A84FFu, 1);
            line(canvas, x, y, x, y + h, 0x660A84FFu, 1);
            line(canvas, x + w - 1, y, x + w - 1, y + h, 0x660A84FFu, 1);
            text(canvas, "Canvas", x + 8, y + 6, M3_TEXT_DIM, font_ui(9));
        }
    }
    else if (strcmp(tag, "Embed") == 0)
    {
        const char *embed = ide_design_attr(&s_design_doc, index, "@embed");
        fill(canvas, x, y, w, h, radius > 0 ? radius : 4, M3_SURFACE_CONTAINER);
        fill(canvas, x, y, w, 1, 0, C_BORDER_SOFT);
        icon_center(canvas, AROMA_ICON_EXTENSION, x + w / 2, y + h / 2 - 8,
                    font_px + 6, C_TEXT_FAINT);
        if (embed && embed[0])
            text_fit(canvas, embed, x + 6, y + h / 2 + 10, w - 12, C_TEXT_DIM,
                     font_ui(8));
        else
            text_fit(canvas, "@embed", x + 6, y + h / 2 + 10, w - 12, C_TEXT_DIM,
                     font_ui(8));
    }
    else if (strcmp(tag, "Container") == 0 || strcmp(tag, "Row") == 0 ||
             strcmp(tag, "Column") == 0)
    {
        if (!preview)
            line(canvas, x, y, x + w, y, 0x4438383Au, 1);
    }
    else if (!preview)
    {
        fill(canvas, x, y, w, h, 3, 0x3318181Au);
        text_fit(canvas, tag, x + 4, y + 4, w - 8, C_TEXT_DIM, font_ui(9));
    }
}

static void designer_draw_model(AromaNode *canvas, const DesignerView *view,
                                bool preview)
{
    fill(canvas, view->origin_x, view->origin_y,
         designer_scaled(s_design_doc.page_width, view->scale),
         designer_scaled(s_design_doc.page_height, view->scale), 0,
         C_EDITOR);
                                                                         
                                                                             
                                  
    if (s_design_doc.root >= 0 &&
        strcmp(s_design_doc.nodes[s_design_doc.root].tag, "Window") != 0)
        designer_draw_node(canvas, view, s_design_doc.root, preview);
    for (int i = s_design_doc.root + 1; i < s_design_doc.node_count; i++)
        designer_draw_node(canvas, view, i, preview);
}

static void designer_toolbar_button(AromaNode *canvas, int x, int w,
                                    const char *icon, const char *label,
                                    bool accent, int hover_id)
{
    int y = EDITOR_Y + 7;
    bool hovered = s_designer_tool_hover == hover_id;
    fill(canvas, x, y, w, DS_TOOL_H, DS_TOOL_H / 2,
         accent ? C_PRIMARY : hovered ? M3_SURFACE_HIGHEST : M3_SURFACE_HIGH);
    icon_center(canvas, icon, x + 16, y + DS_TOOL_H / 2, 13,
                accent ? M3_ON_PRIMARY : C_TEXT_SOFT);
    text_fit(canvas, label, x + 28, y + 6, w - 32,
             accent ? M3_ON_PRIMARY : C_TEXT, font_ui(10));
}

static void designer_draw_toolbar(void)
{
    if (!s_designer_toolbar)
        return;
    aroma_canvas_reset(s_designer_toolbar);
    if (!s_designer_visible)
        return;
    fill(s_designer_toolbar, EDITOR_X, EDITOR_Y, editor_w, DESIGN_TOOLBAR_H,
         0, M3_SURFACE_CONTAINER);
    fill(s_designer_toolbar, EDITOR_X, EDITOR_Y + DESIGN_TOOLBAR_H - 1,
         editor_w, 1, 0, C_BORDER);
    icon_center(s_designer_toolbar, AROMA_ICON_WIDGETS, EDITOR_X + 22,
                EDITOR_Y + 20, 16, C_PRIMARY);
    text(s_designer_toolbar, "UI DESIGNER", EDITOR_X + 36, EDITOR_Y + 11,
         C_TEXT, font_ui(12));
    if (s_active_file >= 0)
        text_fit(s_designer_toolbar, ide_files[s_active_file].path,
                 EDITOR_X + 132, EDITOR_Y + 12, ds_code_x - EDITOR_X - 150,
                 C_TEXT_FAINT, font_mono(10));
                      
    designer_toolbar_button(s_designer_toolbar, ds_code_x, DS_TOOL_W,
                            AROMA_ICON_CODE, "Code", false, 1);
    designer_toolbar_button(s_designer_toolbar, ds_dup_x, 86,
                            AROMA_ICON_CONTENT_COPY, "Duplicate", false, 2);
    designer_toolbar_button(s_designer_toolbar, ds_delete_x, 72,
                            AROMA_ICON_DELETE, "Delete", false, 3);
    designer_toolbar_button(s_designer_toolbar, ds_save_x, 74,
                            AROMA_ICON_SAVE, "Save", true, 4);
                         
    int align_x = ds_save_x + 84;
    int align_w = 62;
    if (align_x + align_w < right_x - 8)
    {
        designer_toolbar_button(s_designer_toolbar, align_x, align_w,
                                AROMA_ICON_FORMAT_ALIGN_LEFT, "L", false, 5);
        designer_toolbar_button(s_designer_toolbar, align_x + align_w + 4, align_w,
                                AROMA_ICON_FORMAT_ALIGN_CENTER, "C", false, 6);
        designer_toolbar_button(s_designer_toolbar, align_x + (align_w + 4) * 2,
                                align_w, AROMA_ICON_FORMAT_ALIGN_RIGHT, "R",
                                false, 7);
    }
}

static void designer_draw_palette(void)
{
    if (!s_designer_palette)
        return;
    aroma_canvas_reset(s_designer_palette);
    if (!s_designer_visible)
        return;
    fill(s_designer_palette, DESIGN_PALETTE_X, design_body_y, DESIGN_PALETTE_W,
         design_body_h, 0, M3_SURFACE_CONTAINER);
    fill(s_designer_palette, DESIGN_PALETTE_X + DESIGN_PALETTE_W - 1,
         design_body_y, 1, design_body_h, 0, C_BORDER);
    int content_h = DESIGN_PALETTE_COUNT * DESIGN_PALETTE_ITEM_H;
    int visible_h = design_body_h - 16;
    int max_scroll = content_h - visible_h;
    if (max_scroll < 0)
        max_scroll = 0;
    if (s_designer_palette_scroll < 0)
        s_designer_palette_scroll = 0;
    if (s_designer_palette_scroll > max_scroll)
        s_designer_palette_scroll = max_scroll;
    int scroll_y = DESIGN_PALETTE_ITEM_Y - s_designer_palette_scroll;
    for (int i = 0; i < DESIGN_PALETTE_COUNT; i++)
    {
        int item_y = scroll_y + i * DESIGN_PALETTE_ITEM_H;
        if (item_y < design_body_y + 8 ||
            item_y + DESIGN_PALETTE_ITEM_H > design_body_y + design_body_h - 8)
            continue;
        bool active = s_designer_palette_hover == i ||
                      s_designer_drag_palette == i;
        if (active)
            fill(s_designer_palette, DESIGN_PALETTE_X + 8, item_y - 3,
                 DESIGN_PALETTE_W - 16, 34, 5,
                 s_designer_drag_palette == i ? M3_PRIMARY_CONTAINER : C_SELECT);
        fill(s_designer_palette, DESIGN_PALETTE_X + 14, item_y + 5, 22, 22, 5,
             M3_SURFACE_HIGH);
        icon_center(s_designer_palette, k_design_palette[i].icon,
                    DESIGN_PALETTE_X + 25, item_y + 16, 13,
                    active ? C_PRIMARY : C_TEXT_DIM);
        text(s_designer_palette, k_design_palette[i].label,
             DESIGN_PALETTE_X + 46, item_y + 7, C_TEXT_SOFT, font_ui(11));
        icon_center(s_designer_palette, AROMA_ICON_ADD,
                    DESIGN_PALETTE_X + DESIGN_PALETTE_W - 20, item_y + 16, 12,
                    C_TEXT_FAINT);
    }
                                                             
    fill(s_designer_palette, DESIGN_PALETTE_X, design_body_y,
         DESIGN_PALETTE_W, 8, 0, M3_SURFACE_CONTAINER);
    fill(s_designer_palette, DESIGN_PALETTE_X,
         design_body_y + design_body_h - 8, DESIGN_PALETTE_W, 8, 0,
         M3_SURFACE_CONTAINER);
}

                                                                          
                                                                      
static bool designer_preview_set_rect(int x, int y, int width, int height)
{
    if (!s_designer_preview)
        return false;
    AromaRect *r = aroma_node_get_rect(s_designer_preview);
    if (!r)
        return false;
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    bool moved = r->x != x || r->y != y || r->width != width ||
                 r->height != height;
    r->x = x;
    r->y = y;
    r->width = width;
    r->height = height;
    return moved;
}

static void designer_preview_clear(void)
{
    if (s_designer_preview)
        aroma_node_remove_all_children(s_designer_preview);
    if (s_designer_preview_registry)
    {
        IncenseFreeRegistry(s_designer_preview_registry);
        s_designer_preview_registry = NULL;
    }
    s_designer_preview_ok = false;
    s_designer_preview_dirty = true;
    if (s_designer_preview)
        aroma_node_set_hidden(s_designer_preview, true);
}

                                                                         
                                                                            
                                          
static bool designer_preview_hit_test(AromaNode *node, int x, int y,
                                      void *user_data)
{
    (void)node;
    (void)x;
    (void)y;
    (void)user_data;
    /* The preview is a visual snapshot.  Let designer gestures fall through
       to the canvas instead of focusing or pressing the rendered widgets. */
    return false;
}

static void designer_preview_offset_z(AromaNode *node)
{
    if (!node)
        return;
    for (uint64_t i = 0; i < node->child_count; i++)
    {
        AromaNode *child = node->child_nodes[i];
        if (!child)
            continue;
        int z = aroma_node_get_z_index(child);
        if (z < 0)
            z = 0;
        if (z > 93)
            z = 93;
        aroma_node_set_z_index(child, 6 + z);
        designer_preview_offset_z(child);
    }
}

static void designer_preview_refresh(void)
{
    if (!s_designer_preview)
        return;
    aroma_node_set_hit_test_cb(s_designer_preview, designer_preview_hit_test,
                               NULL);
    if (s_designer_dragging)
        return;
    if (s_designer_preview_refreshing)
        return;
    if (!s_designer_visible || !s_design_doc.valid ||
        s_design_doc.node_count <= 0)
    {
        aroma_node_set_hidden(s_designer_preview, true);
        return;
    }

    DesignerView view;
    designer_canvas_view(&view);
    int page_w = designer_scaled(s_design_doc.page_width, view.scale);
    int page_h = designer_scaled(s_design_doc.page_height, view.scale);
    if (page_w < 1)
        page_w = 1;
    if (page_h < 1)
        page_h = 1;
    bool moved = designer_preview_set_rect(view.origin_x, view.origin_y,
                                           page_w, page_h);

    if (!s_designer_preview_dirty && s_designer_preview_ok &&
        s_designer_preview_scale == view.scale)
    {
        if (moved)
            aroma_node_update_layout(s_designer_preview, view.origin_x,
                                     view.origin_y, page_w, page_h);
        aroma_node_set_hidden(s_designer_preview, false);
        return;
    }

    s_designer_preview_refreshing = true;
    designer_preview_clear();
    s_designer_preview_scale = view.scale;
    s_designer_preview_dirty = false;
    designer_preview_set_rect(view.origin_x, view.origin_y, page_w, page_h);

    enum
    {
        DESIGNER_PREVIEW_SOURCE_MAX = 256 * 1024
    };
    char *source = (char *)malloc(DESIGNER_PREVIEW_SOURCE_MAX);
    if (!source)
    {
        s_designer_preview_refreshing = false;
        return;
    }
    if (!ide_design_serialize_scaled(&s_design_doc, view.scale, source,
                                     DESIGNER_PREVIEW_SOURCE_MAX))
    {
        free(source);
        s_designer_preview_refreshing = false;
        return;
    }

    IncenseRegistry *registry = NULL;
    bool ok = IncenseLoadStringIntoParent(
        source, s_designer_preview, s_assets ? s_assets->editor : NULL,
        s_assets ? s_assets->icons[0] : NULL, &registry);
    free(source);
    (void)ok;

    /* Keep whatever the real widget loader managed to build.  A single
       unsupported or missing asset should not replace the whole preview with
       a painted mock-up; the designer always edits live widget instances. */
    if (s_designer_preview->child_count == 0)
    {
        if (registry)
            IncenseFreeRegistry(registry);
        aroma_node_remove_all_children(s_designer_preview);
        aroma_node_set_hidden(s_designer_preview, true);
        s_designer_preview_refreshing = false;
        return;
    }

    s_designer_preview_registry = registry;
    s_designer_preview_ok = true;
    designer_preview_offset_z(s_designer_preview);
    aroma_node_update_layout(s_designer_preview, view.origin_x, view.origin_y,
                             page_w, page_h);
    aroma_node_set_hidden(s_designer_preview, false);
    s_designer_preview_refreshing = false;
}

static void designer_draw_overlay(void)
{
    if (!s_designer_overlay)
        return;
    aroma_canvas_reset(s_designer_overlay);
    if (!s_designer_visible)
        return;

    DesignerView view;
    designer_canvas_view(&view);
    int page_w = designer_scaled(s_design_doc.page_width, view.scale);
    int page_h = designer_scaled(s_design_doc.page_height, view.scale);

    if (page_w > 0 && page_h > 0)
    {
                                                                          
                                                                           
        int top_h = view.origin_y - design_body_y;
        if (top_h > 0)
            fill(s_designer_overlay, DESIGN_CANVAS_X, design_body_y,
                 design_canvas_w, top_h, 0, M3_SURFACE_LOWEST);
        int bottom_y = view.origin_y + page_h;
        int bottom_h = design_body_y + design_body_h - bottom_y;
        if (bottom_h > 0)
            fill(s_designer_overlay, DESIGN_CANVAS_X, bottom_y, design_canvas_w,
                 bottom_h, 0, M3_SURFACE_LOWEST);
        int left_w = view.origin_x - DESIGN_CANVAS_X;
        if (left_w > 0)
            fill(s_designer_overlay, DESIGN_CANVAS_X, view.origin_y, left_w,
                 page_h, 0, M3_SURFACE_LOWEST);
        int right_x = view.origin_x + page_w;
        int right_w = DESIGN_CANVAS_X + design_canvas_w - right_x;
        if (right_w > 0)
            fill(s_designer_overlay, right_x, view.origin_y, right_w, page_h, 0,
                 M3_SURFACE_LOWEST);

        line(s_designer_overlay, view.origin_x, view.origin_y,
             view.origin_x + page_w, view.origin_y, M3_SURFACE_HIGH, 1);
        line(s_designer_overlay, view.origin_x, view.origin_y + page_h,
             view.origin_x + page_w, view.origin_y + page_h, M3_SURFACE_HIGH,
             1);
        line(s_designer_overlay, view.origin_x, view.origin_y, view.origin_x,
             view.origin_y + page_h, M3_SURFACE_HIGH, 1);
        line(s_designer_overlay, view.origin_x + page_w, view.origin_y,
             view.origin_x + page_w, view.origin_y + page_h, M3_SURFACE_HIGH,
             1);
    }

    text(s_designer_overlay, "PAGE", DESIGN_CANVAS_X + 14, design_body_y + 14,
         C_TEXT_DIM, font_ui(10));
    char zoom[32];
    snprintf(zoom, sizeof(zoom), "%d%%", (view.scale * 100) / 256);
    text(s_designer_overlay, zoom, DESIGN_CANVAS_X + design_canvas_w - 44,
         design_body_y + 14, C_TEXT_FAINT, font_ui(10));
    text(s_designer_overlay, s_designer_preview_ok ? "LIVE" : "NO PREVIEW",
         DESIGN_CANVAS_X + design_canvas_w - 92, design_body_y + 14,
         s_designer_preview_ok ? C_PRIMARY : C_TEXT_FAINT, font_ui(9));

    int selected = s_design_doc.selected;
    if (selected >= 0 && selected < s_design_doc.node_count &&
        (selected > 0 ||
         strcmp(s_design_doc.nodes[s_design_doc.root].tag, "Window") != 0))
    {
        const IdeDesignNode *node = &s_design_doc.nodes[selected];
        int x = view.origin_x + designer_scaled(node->abs_x, view.scale);
        int y = view.origin_y + designer_scaled(node->abs_y, view.scale);
        int w = designer_scaled(node->abs_width, view.scale);
        int h = designer_scaled(node->abs_height, view.scale);
        if (w < 4)
            w = 4;
        if (h < 4)
            h = 4;
        line(s_designer_overlay, x, y, x + w, y, C_PRIMARY, 2);
        line(s_designer_overlay, x, y + h, x + w, y + h, C_PRIMARY, 2);
        line(s_designer_overlay, x, y, x, y + h, C_PRIMARY, 2);
        line(s_designer_overlay, x + w, y, x + w, y + h, C_PRIMARY, 2);
        fill(s_designer_overlay, x - 4, y - 4, 8, 8, 4, C_PRIMARY);
        fill(s_designer_overlay, x + w - 4, y - 4, 8, 8, 4, C_PRIMARY);
        fill(s_designer_overlay, x - 4, y + h - 4, 8, 8, 4, C_PRIMARY);
        fill(s_designer_overlay, x + w - 4, y + h - 4, 8, 8, 4, C_PRIMARY);
        char badge[64];
        snprintf(badge, sizeof(badge), "%s", node->tag);
        int badge_w = text_width(font_ui(9), badge) + 10;
        fill(s_designer_overlay, x, y - 20, badge_w, 18, 9, C_PRIMARY);
        text(s_designer_overlay, badge, x + 5, y - 17, M3_ON_PRIMARY,
             font_ui(9));
    }
}

static void designer_draw_canvas(void)
{
    if (!s_designer_canvas)
        return;
    designer_preview_refresh();
    aroma_canvas_reset(s_designer_canvas);
    if (!s_designer_visible)
    {
        designer_draw_overlay();
        return;
    }
    fill(s_designer_canvas, DESIGN_CANVAS_X, design_body_y, design_canvas_w,
         design_body_h, 0, M3_SURFACE_LOWEST);

    DesignerView view;
    designer_canvas_view(&view);
    int page_w = designer_scaled(s_design_doc.page_width, view.scale);
    int page_h = designer_scaled(s_design_doc.page_height, view.scale);

    int frame_x = DESIGN_CANVAS_X + 8;
    int frame_y = design_body_y + 32;
    int frame_w = design_canvas_w - 16;
    int frame_h = design_body_h - 42;
    fill(s_designer_canvas, frame_x, frame_y, frame_w, frame_h, 10,
         M3_SURFACE_LOWEST);
    fill(s_designer_canvas, frame_x, frame_y, frame_w, 1, 0, M3_SURFACE_HIGH);
    fill(s_designer_canvas, frame_x, frame_y + frame_h - 1, frame_w, 1, 0,
         M3_SURFACE_HIGH);

    if (page_w > 0 && page_h > 0)
        fill(s_designer_canvas, view.origin_x, view.origin_y, page_w, page_h,
             0, C_EDITOR);
    designer_draw_overlay();
}

static void designer_draw_all(void)
{
    if (!s_designer_visible)
        return;
    designer_draw_toolbar();
    designer_draw_palette();
    designer_draw_canvas();
}

static void designer_history_clear(void)
{
    for (int i = 0; i < s_design_undo_count; i++)
    {
        free(s_design_undo[i]);
        s_design_undo[i] = NULL;
    }
    s_design_undo_count = 0;
    s_design_undo_pos = -1;
    s_designer_drag_last_update = 0;
}

                                                                              
                                                                             
static void designer_reset_cache(void)
{
    ide_design_init(&s_design_doc);
    s_design_error[0] = '\0';

    designer_history_clear();

    if (s_design_clipboard)
    {
        free(s_design_clipboard);
        s_design_clipboard = NULL;
    }

    s_designer_visible = false;
    s_designer_available = false;
    s_designer_committing = false;
    s_designer_preview_refreshing = false;
    designer_preview_clear();
    s_designer_preview_dirty = true;
    s_designer_palette_hover = -1;
    s_designer_tool_hover = -1;
    s_designer_drag_palette = -1;
    s_designer_drag_node = -1;
    s_designer_dragging = false;
    s_designer_drag_page_valid = false;
    s_designer_drag_moved = false;
    s_designer_resizing = false;
    s_designer_resize_node = -1;
    s_designer_last_click_time = 0;
    s_designer_last_click_node = -1;
    s_designer_palette_scroll = 0;

    s_prop_edit_index = -1;
    s_prop_edit_row = -1;
    s_prop_edit_buf[0] = '\0';
    s_prop_edit_active = false;
    s_prop_dropdown_index = -1;
    s_prop_dropdown_row = -1;
    s_prop_dropdown_open = false;
    s_prop_dropdown_extra_h = 0;
    s_prop_last_selected = -1;
    s_prop_last_panel_y = 0;
    s_prop_cat_count = 0;
    for (int i = 0; i < 16; i++)
    {
        s_prop_cats[i].name = NULL;
        s_prop_cats[i].start_row = 0;
        s_prop_cats[i].row_count = 0;
        s_prop_cats[i].expanded = true;
    }
    for (int i = 0; i < 48; i++)
    {
        s_prop_row_widgets[i] = NULL;
        s_prop_label_widgets[i] = NULL;
        s_prop_value_widgets[i] = NULL;
        s_prop_dropdown_widgets[i] = NULL;
        s_prop_dropdown_widget_count[i] = 0;
    }
    for (int i = 0; i < 16; i++)
        s_prop_category_widgets[i] = NULL;
}

static int designer_history_push(void)
{
    char *serialized = (char *)malloc(65536);
    if (!serialized)
        return 0;
    if (!ide_design_serialize(&s_design_doc, serialized, 65536))
    {
        free(serialized);
        return 0;
    }
    if (s_design_undo_pos < s_design_undo_count - 1)
    {
        for (int i = s_design_undo_pos + 1; i < s_design_undo_count; i++)
        {
            free(s_design_undo[i]);
            s_design_undo[i] = NULL;
        }
        s_design_undo_count = s_design_undo_pos + 1;
    }
    if (s_design_undo_count >= 16)
    {
        free(s_design_undo[0]);
        memmove(s_design_undo, s_design_undo + 1,
                sizeof(s_design_undo[0]) * 15);
        s_design_undo_count--;
        s_design_undo_pos--;
    }
    s_design_undo[s_design_undo_count++] = serialized;
    s_design_undo_pos = s_design_undo_count - 1;
    return 1;
}

static void designer_after_mutation(void)
{
    designer_history_push();
    designer_apply_to_file();
    s_designer_preview_dirty = true;
    designer_draw_all();
    draw_outline();
    draw_tabs();
    draw_sidebar();
    draw_status();
    draw_breadcrumb();
    if (s_panel_visible && s_panel_tab == 0)
        draw_panel();
}

static void designer_restore_history_state(void)
{
    if (s_design_undo_pos < 0 || s_design_undo_pos >= s_design_undo_count)
        return;
    if (ide_design_parse(&s_design_doc, s_design_undo[s_design_undo_pos]))
    {
        s_design_doc.selected = -1;
        designer_apply_to_file();
        s_designer_preview_dirty = true;
        designer_draw_all();
        draw_outline();
        draw_tabs();
        draw_sidebar();
        draw_status();
        draw_breadcrumb();
        if (s_panel_visible && s_panel_tab == 0)
            draw_panel();
    }
}

static bool designer_history_undo(void)
{
    if (s_design_undo_pos <= 0)
        return false;
    s_design_undo_pos--;
    designer_restore_history_state();
    return true;
}

static bool designer_history_redo(void)
{
    if (s_design_undo_pos < 0 || s_design_undo_pos + 1 >= s_design_undo_count)
        return false;
    s_design_undo_pos++;
    designer_restore_history_state();
    return true;
}

static void designer_apply_to_file(void)
{
    if (!s_designer_visible || s_active_file < 0 || s_active_file >= ide_file_count)
        return;
    char *serialized = (char *)malloc(65536);
    if (!serialized)
        return;
    if (!ide_design_serialize(&s_design_doc, serialized, 65536))
    {
        free(serialized);
        toast("Could not serialize the designer document");
        return;
    }
    IdeFile *file = &ide_files[s_active_file];
    if (!file->text || strcmp(file->text, serialized) != 0)
    {
        free(file->text);
        file->text = strdup(serialized);
        file->dirty = true;
        ide_lsp_refresh_diagnostics(s_active_file);
        aroma_code_editor_set_diagnostics(s_editor, file->diagnostics,
                                          file->diagnostic_count);
        refresh_outline();
    }
    free(serialized);
}

static void designer_set_nodes_visible(bool visible)
{
    AromaNode *nodes[] = {s_designer_toolbar, s_designer_palette,
                          s_designer_canvas, s_designer_preview,
                          s_designer_overlay};
    for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++)
    {
        if (nodes[i])
            aroma_node_set_hidden(nodes[i], !visible);
    }
    if (!visible)
        designer_preview_clear();
    if (s_editor)
        aroma_node_set_hidden(s_editor, visible);
                                                                           
                                                                        
    if (s_editor_bg)
        aroma_node_set_hidden(s_editor_bg, false);
}

static void designer_set_visible(bool visible)
{
    if (visible)
    {
        if (s_designer_visible || s_active_file < 0 ||
            s_active_file >= ide_file_count ||
            !designer_is_markup_path(ide_files[s_active_file].path))
            return;
        if (!ide_design_parse(&s_design_doc,
                              ide_files[s_active_file].text
                                  ? ide_files[s_active_file].text
                                  : ""))
        {
            snprintf(s_design_error, sizeof(s_design_error), "%s",
                     s_design_doc.error);
            toast("Designer could not parse this file");
            designer_set_nodes_visible(false);
            return;
        }
        s_designer_available = true;
        s_designer_visible = true;
        s_designer_drag_palette = -1;
        s_designer_drag_node = -1;
        s_designer_dragging = false;
        s_designer_drag_page_valid = false;
        s_designer_drag_moved = false;
        s_designer_resizing = false;
        s_designer_resize_node = -1;
        s_designer_palette_hover = -1;
        s_designer_tool_hover = -1;
        s_designer_preview_dirty = true;
        designer_set_nodes_visible(true);
        designer_history_clear();
        designer_history_push();
        if (s_designer_overlay)
            aroma_ui_set_focused_node(s_designer_overlay);
        else if (s_designer_canvas)
            aroma_ui_set_focused_node(s_designer_canvas);
        s_outline_scroll = 0;
        s_outline_hover = -1;
        designer_draw_all();
        draw_outline();
        draw_breadcrumb();
        return;
    }

    if (!s_designer_visible)
        return;
    s_prop_edit_active = false;
    designer_apply_to_file();
    s_designer_visible = false;
    if (s_dialog_open && s_dialog_mode == IDE_DIALOG_PROPERTIES)
        close_dialog();
    s_designer_drag_palette = -1;
    s_designer_drag_node = -1;
    s_designer_drag_page_valid = false;
    s_designer_drag_moved = false;
    s_designer_resizing = false;
    s_designer_resize_node = -1;
    designer_set_nodes_visible(false);
    if (s_active_file >= 0 && s_active_file < ide_file_count && s_editor)
    {
        IdeFile *file = &ide_files[s_active_file];
        s_designer_committing = true;
        aroma_code_editor_set_text(s_editor, file->text ? file->text : "");
        s_designer_committing = false;
        aroma_code_editor_set_diagnostics(s_editor, file->diagnostics,
                                          file->diagnostic_count);
        refresh_outline();
        aroma_code_editor_focus(s_editor);
    }
    designer_draw_all();
    draw_outline();
    draw_all();
}

static void designer_sync_for_file(void)
{
    s_designer_available = false;
    if (s_active_file < 0 || s_active_file >= ide_file_count)
    {
        if (s_designer_visible)
            designer_set_visible(false);
        return;
    }
    if (!designer_is_markup_path(ide_files[s_active_file].path))
    {
        if (s_designer_visible)
            designer_set_visible(false);
        return;
    }
    s_designer_available = true;
    if (!s_designer_visible)
        designer_set_visible(true);
}

static int designer_palette_item_at(int x, int y)
{
    if (x < DESIGN_PALETTE_X + 8 || x >= DESIGN_PALETTE_X + DESIGN_PALETTE_W - 8)
        return -1;
    if (y < design_body_y + 8 ||
        y >= design_body_y + design_body_h - 8)
        return -1;
    for (int i = 0; i < DESIGN_PALETTE_COUNT; i++)
    {
        int item_y = DESIGN_PALETTE_ITEM_Y - s_designer_palette_scroll +
                     i * DESIGN_PALETTE_ITEM_H;
        if (y >= item_y && y < item_y + DESIGN_PALETTE_ITEM_H &&
            item_y >= design_body_y + 8 &&
            item_y + DESIGN_PALETTE_ITEM_H <= design_body_y + design_body_h - 8)
            return i;
    }
    return -1;
}

static void designer_add_from_palette(int page_x, int page_y)
{
    if (s_designer_drag_palette < 0 ||
        s_designer_drag_palette >= DESIGN_PALETTE_COUNT)
        return;
                                                
    page_x = (page_x / 8) * 8;
    page_y = (page_y / 8) * 8;
    int index = ide_design_add(&s_design_doc,
                               k_design_palette[s_designer_drag_palette].name,
                               page_x, page_y);
    if (index < 0)
    {
        toast("Cannot add a widget here");
        return;
    }
    s_design_doc.selected = index;
    s_designer_drag_palette = -1;
    s_designer_drag_page_valid = false;
    designer_after_mutation();
}

static void designer_align_selected(const char *mode);
static bool designer_toolbar_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_designer_visible)
        return false;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    if (y < EDITOR_Y || y >= EDITOR_Y + DESIGN_TOOLBAR_H)
        return true;
    if (x >= ds_code_x && x < ds_code_x + DS_TOOL_W)
    {
        designer_set_visible(false);
        return true;
    }
    if (x >= ds_dup_x && x < ds_dup_x + 86)
    {
        if (s_design_doc.selected > 0)
        {
            if (ide_design_duplicate(&s_design_doc, s_design_doc.selected) >= 0)
                designer_after_mutation();
            else
                toast("Cannot duplicate this element");
        }
        else
            toast("Select a widget to duplicate");
        return true;
    }
    if (x >= ds_delete_x && x < ds_delete_x + 72)
    {
        if (s_design_doc.selected > 0)
        {
            if (ide_design_remove(&s_design_doc, s_design_doc.selected))
                designer_after_mutation();
        }
        else
            toast("Select a widget to delete");
        return true;
    }
    if (x >= ds_save_x && x < ds_save_x + 74)
    {
        designer_apply_to_file();
        save_file(s_active_file);
        if (s_preview_visible)
            preview_window_create();
        return true;
    }
                         
    int align_x = ds_save_x + 84;
    int align_w = 62;
    if (x >= align_x && x < align_x + align_w)
    {
        designer_align_selected("left");
        return true;
    }
    if (x >= align_x + align_w + 4 && x < align_x + (align_w + 4) * 2)
    {
        designer_align_selected("center");
        return true;
    }
    if (x >= align_x + (align_w + 4) * 2 && x < align_x + (align_w + 4) * 3)
    {
        designer_align_selected("right");
        return true;
    }
    return true;
}

static bool designer_toolbar_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_designer_visible)
        return false;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int hover = -1;
    if (y >= EDITOR_Y + 7 && y < EDITOR_Y + 7 + DS_TOOL_H)
    {
        if (x >= ds_code_x && x < ds_code_x + DS_TOOL_W)
            hover = 1;
        else if (x >= ds_dup_x && x < ds_dup_x + 86)
            hover = 2;
        else if (x >= ds_delete_x && x < ds_delete_x + 72)
            hover = 3;
        else if (x >= ds_save_x && x < ds_save_x + 74)
            hover = 4;
        else
        {
            int align_x = ds_save_x + 84;
            int align_w = 62;
            if (x >= align_x && x < align_x + align_w)
                hover = 5;
            else if (x >= align_x + align_w + 4 &&
                     x < align_x + (align_w + 4) * 2)
                hover = 6;
            else if (x >= align_x + (align_w + 4) * 2 &&
                     x < align_x + (align_w + 4) * 3)
                hover = 7;
        }
    }
    if (hover != s_designer_tool_hover)
    {
        s_designer_tool_hover = hover;
        designer_draw_toolbar();
    }
    return true;
}

static void designer_align_selected(const char *mode)
{
    if (s_design_doc.selected <= 0 ||
        s_design_doc.selected >= s_design_doc.node_count)
        return;
                                                                      
    int indices[16];
    int count = 1;
    indices[0] = s_design_doc.selected;

    int min_x = INT32_MAX, min_y = INT32_MAX;
    int max_x = INT32_MIN, max_y = INT32_MIN;
    for (int i = 0; i < count; i++)
    {
        IdeDesignNode *n = &s_design_doc.nodes[indices[i]];
        int ax = n->abs_x, ay = n->abs_y;
        int aw = n->abs_width, ah = n->abs_height;
        if (ax < min_x)
            min_x = ax;
        if (ay < min_y)
            min_y = ay;
        if (ax + aw > max_x)
            max_x = ax + aw;
        if (ay + ah > max_y)
            max_y = ay + ah;
    }

    for (int i = 0; i < count; i++)
    {
        IdeDesignNode *n = &s_design_doc.nodes[indices[i]];
        int ax = n->abs_x, ay = n->abs_y;
        int aw = n->abs_width, ah = n->abs_height;
        int nx = ax, ny = ay;
        if (strcmp(mode, "left") == 0)
            nx = min_x;
        else if (strcmp(mode, "center") == 0)
            nx = min_x + (max_x - min_x - aw) / 2;
        else if (strcmp(mode, "right") == 0)
            nx = max_x - aw;
        int parent = n->parent;
        int parent_x = parent >= 0 ? s_design_doc.nodes[parent].abs_x : 0;
        int parent_y = parent >= 0 ? s_design_doc.nodes[parent].abs_y : 0;
        char value[32];
        snprintf(value, sizeof(value), "%d", nx - parent_x);
        ide_design_set_attr(&s_design_doc, indices[i], "x", value, false);
        snprintf(value, sizeof(value), "%d", ny - parent_y);
        ide_design_set_attr(&s_design_doc, indices[i], "y", value, false);
    }
    ide_design_layout(&s_design_doc);
    designer_after_mutation();
}

static bool designer_toolbar_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_designer_tool_hover != -1)
    {
        s_designer_tool_hover = -1;
        designer_draw_toolbar();
    }
    return true;
}

static bool designer_palette_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_designer_visible)
        return false;
    if (event->event_type == EVENT_TYPE_MOUSE_SCROLL)
    {
        int dy = (int)(event->data.mouse.scroll_y * DESIGN_PALETTE_ITEM_H);
        s_designer_palette_scroll += dy;
        int content_h = DESIGN_PALETTE_COUNT * DESIGN_PALETTE_ITEM_H;
        int visible_h = design_body_h - 16;
        int max_scroll = content_h - visible_h;
        if (max_scroll < 0)
            max_scroll = 0;
        if (s_designer_palette_scroll < 0)
            s_designer_palette_scroll = 0;
        if (s_designer_palette_scroll > max_scroll)
            s_designer_palette_scroll = max_scroll;
        designer_draw_palette();
        return true;
    }
    int hover = designer_palette_item_at(event->data.mouse.x,
                                         event->data.mouse.y);
    if (hover != s_designer_palette_hover)
    {
        s_designer_palette_hover = hover;
        designer_draw_palette();
    }
    return true;
}

static bool designer_palette_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_designer_visible)
        return false;
    int item = designer_palette_item_at(event->data.mouse.x,
                                        event->data.mouse.y);
    if (item >= 0)
    {
        s_designer_drag_palette = item;
        s_designer_drag_node = -1;
        s_designer_drag_page_valid = false;
        s_designer_drag_moved = false;
        designer_draw_palette();
        designer_draw_canvas();
        toast("Drop %s on the page", k_design_palette[item].label);
    }
    return true;
}

static bool designer_palette_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_designer_palette_hover != -1)
    {
        s_designer_palette_hover = -1;
        designer_draw_palette();
    }
    return true;
}

static bool designer_canvas_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_designer_visible)
        return false;
    DesignerView view;
    designer_canvas_view(&view);
    int page_x = 0;
    int page_y = 0;
    bool inside = designer_view_point(&view, event->data.mouse.x,
                                      event->data.mouse.y, &page_x, &page_y);
    if (!inside)
    {
        s_prop_edit_active = false;
        return true;
    }

    if (s_designer_drag_palette >= 0)
    {
        designer_add_from_palette(page_x, page_y);
        return true;
    }

    int hit = ide_design_hit_test(&s_design_doc, page_x, page_y);
    int mouse_x = event->data.mouse.x;
    int mouse_y = event->data.mouse.y;

                                                
    uint64_t now = now_ms();
    if (hit >= 0 && hit < s_design_doc.node_count &&
        s_designer_last_click_node == hit &&
        now - s_designer_last_click_time < 500)
    {
        s_designer_last_click_node = -1;
        s_design_doc.selected = hit;
        open_dialog(IDE_DIALOG_PROPERTIES, "");
        return true;
    }
    if (hit >= 0 && hit < s_design_doc.node_count)
    {
        s_designer_last_click_node = hit;
        s_designer_last_click_time = now;
    }

                                                                        
    int selected = s_design_doc.selected;
    if (selected > 0 && selected < s_design_doc.node_count)
    {
        const IdeDesignNode *node = &s_design_doc.nodes[selected];
        DesignerView view;
        designer_canvas_view(&view);
        int rx = view.origin_x + designer_scaled(node->abs_x + node->abs_width, view.scale);
        int ry = view.origin_y + designer_scaled(node->abs_y + node->abs_height, view.scale);
        if (mouse_x >= rx - 6 && mouse_x <= rx + 4 && mouse_y >= ry - 6 && mouse_y <= ry + 4)
        {
            s_designer_resizing = true;
            s_designer_dragging = true;
            s_designer_resize_node = selected;
            s_designer_resize_start_x = page_x;
            s_designer_resize_start_y = page_y;
            s_designer_resize_start_w = node->abs_width;
            s_designer_resize_start_h = node->abs_height;
            s_designer_drag_node = -1;
            designer_draw_canvas();
            return true;
        }
    }

    if (hit < 0)
    {
        s_design_doc.selected = -1;
        s_designer_drag_node = -1;
    }
    else
    {
        s_design_doc.selected = hit;
        s_designer_drag_node = hit;
        s_designer_dragging = true;
        int parent = s_design_doc.nodes[hit].parent;
        int parent_x = parent >= 0 ? s_design_doc.nodes[parent].abs_x : 0;
        int parent_y = parent >= 0 ? s_design_doc.nodes[parent].abs_y : 0;
        s_designer_drag_offset_x = page_x - (parent_x + s_design_doc.nodes[hit].x);
        s_designer_drag_offset_y = page_y - (parent_y + s_design_doc.nodes[hit].y);
        s_designer_drag_moved = false;
    }
    s_designer_drag_page_valid = false;
    designer_draw_palette();
    designer_draw_canvas();
    return true;
}

static bool designer_canvas_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_designer_visible)
        return false;
    DesignerView view;
    designer_canvas_view(&view);
    int page_x = 0;
    int page_y = 0;
    bool inside = designer_view_point(&view, event->data.mouse.x,
                                      event->data.mouse.y, &page_x, &page_y);
    s_designer_drag_page_valid = inside;
    s_designer_drag_page_x = page_x;
    s_designer_drag_page_y = page_y;
    if (s_designer_resizing && s_designer_resize_node > 0 &&
        s_designer_resize_node < s_design_doc.node_count)
    {
        IdeDesignNode *node = &s_design_doc.nodes[s_designer_resize_node];
        int parent = node->parent;
        int max_w = (parent >= 0 ? s_design_doc.nodes[parent].abs_width
                                 : s_design_doc.page_width) - node->x;
        int max_h = (parent >= 0 ? s_design_doc.nodes[parent].abs_height
                                 : s_design_doc.page_height) - node->y;
        int new_w = s_designer_resize_start_w + (page_x - s_designer_resize_start_x);
        int new_h = s_designer_resize_start_h + (page_y - s_designer_resize_start_y);
        new_w = (new_w / 8) * 8;
        new_h = (new_h / 8) * 8;
        int min_w = max_w < 40 ? (max_w > 8 ? max_w : 8) : 40;
        int min_h = max_h < 30 ? (max_h > 8 ? max_h : 8) : 30;
        if (new_w < min_w)
            new_w = min_w;
        if (new_h < min_h)
            new_h = min_h;
        if (max_w > 0 && new_w > max_w)
            new_w = max_w;
        if (max_h > 0 && new_h > max_h)
            new_h = max_h;
        char value[32];
        snprintf(value, sizeof(value), "%d", new_w);
        ide_design_set_attr(&s_design_doc, s_designer_resize_node, "width", value, false);
        snprintf(value, sizeof(value), "%d", new_h);
        ide_design_set_attr(&s_design_doc, s_designer_resize_node, "height", value, false);
        ide_design_layout(&s_design_doc);
        s_designer_drag_moved = true;
        s_designer_preview_dirty = true;
        designer_draw_canvas();
        return true;
    }
    if (inside && s_designer_drag_node > 0 &&
        s_designer_drag_node < s_design_doc.node_count)
    {
        int target_x = page_x - s_designer_drag_offset_x;
        int target_y = page_y - s_designer_drag_offset_y;
        IdeDesignNode *node = &s_design_doc.nodes[s_designer_drag_node];
        int parent = node->parent;
        int parent_x = parent >= 0 ? s_design_doc.nodes[parent].abs_x : 0;
        int parent_y = parent >= 0 ? s_design_doc.nodes[parent].abs_y : 0;
        int old_x = parent_x + node->x;
        int old_y = parent_y + node->y;
        if (target_x != old_x || target_y != old_y)
        {
                                                                        
            uint64_t now = now_ms();
            if (now - s_designer_drag_last_update >= 16)
            {
                s_designer_drag_last_update = now;
                ide_design_set_position_fast(&s_design_doc, s_designer_drag_node,
                                             target_x, target_y);
                s_designer_drag_moved = true;
                s_designer_preview_dirty = true;
                designer_draw_canvas();
            }
        }
    }
    else if (s_designer_drag_palette >= 0)
    {
    }
    return true;
}

static bool designer_canvas_on_release(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_designer_visible)
        return false;
    DesignerView view;
    designer_canvas_view(&view);
    int page_x = 0;
    int page_y = 0;
    bool inside = designer_view_point(&view, event->data.mouse.x,
                                      event->data.mouse.y, &page_x, &page_y);
    if (s_designer_drag_palette >= 0 && inside)
    {
        designer_add_from_palette(page_x, page_y);
        return true;
    }
    if (s_designer_drag_palette >= 0 && !inside)
    {
        return true;
    }
    if (s_designer_resizing && s_designer_resize_node > 0 &&
        s_designer_resize_node < s_design_doc.node_count)
    {
        if (s_designer_drag_moved)
        {
            designer_history_push();
            designer_apply_to_file();
            draw_tabs();
            draw_sidebar();
            draw_status();
        }
        s_designer_resizing = false;
        s_designer_dragging = false;
        s_designer_resize_node = -1;
        s_designer_drag_moved = false;
        ide_design_layout(&s_design_doc);
        s_designer_preview_dirty = true;
        designer_draw_all();
        return true;
    }
    if (s_designer_drag_node > 0 &&
        s_designer_drag_node < s_design_doc.node_count)
    {
        if (s_designer_drag_moved)
        {
            designer_history_push();
            designer_apply_to_file();
            draw_tabs();
            draw_sidebar();
            draw_status();
        }
        s_designer_drag_node = -1;
        s_designer_dragging = false;
        s_designer_drag_moved = false;
        ide_design_layout(&s_design_doc);
        s_designer_preview_dirty = true;
        designer_draw_all();
    }
    s_designer_drag_page_valid = false;
    return true;
}

static bool designer_canvas_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_designer_resizing && s_designer_resize_node > 0)
    {
        if (s_designer_drag_moved)
        {
            designer_history_push();
            designer_apply_to_file();
            draw_tabs();
            draw_sidebar();
            draw_status();
        }
        s_designer_resizing = false;
        s_designer_dragging = false;
        s_designer_resize_node = -1;
        s_designer_drag_moved = false;
        ide_design_layout(&s_design_doc);
    }
    if (s_designer_drag_node > 0 &&
        s_designer_drag_node < s_design_doc.node_count)
    {
        if (s_designer_drag_moved)
        {
            designer_history_push();
            designer_apply_to_file();
            draw_tabs();
            draw_sidebar();
            draw_status();
        }
        s_designer_drag_node = -1;
        s_designer_dragging = false;
        s_designer_drag_moved = false;
        ide_design_layout(&s_design_doc);
    }
    if (s_designer_drag_palette >= 0)
    {
        s_designer_dragging = false;
        s_designer_drag_palette = -1;
        s_designer_drag_page_valid = false;
        s_designer_drag_moved = false;
        designer_draw_palette();
        designer_draw_canvas();
    }
    return true;
}

static void designer_copy_selected(void);
static void designer_paste_selected(void);
static bool designer_delete_selected(void)
{
    if (!s_designer_visible || s_design_doc.selected <= 0)
        return false;
    if (!ide_design_remove(&s_design_doc, s_design_doc.selected))
        return false;
    designer_after_mutation();
    return true;
}

static void designer_duplicate_selected(void)
{
    if (!s_designer_visible || s_design_doc.selected <= 0)
        return;
    if (ide_design_duplicate(&s_design_doc, s_design_doc.selected) >= 0)
        designer_after_mutation();
}

static void designer_nudge_selected(int dx, int dy)
{
    if (!s_designer_visible || s_design_doc.selected <= 0)
        return;
    IdeDesignNode *node = &s_design_doc.nodes[s_design_doc.selected];
    int parent = node->parent;
    int parent_x = parent >= 0 ? s_design_doc.nodes[parent].abs_x : 0;
    int parent_y = parent >= 0 ? s_design_doc.nodes[parent].abs_y : 0;
    int x = parent_x + node->x + dx;
    int y = parent_y + node->y + dy;
    ide_design_set_position(&s_design_doc, s_design_doc.selected, x, y);
    designer_after_mutation();
}

static void designer_copy_selected(void)
{
    if (!s_designer_visible || s_design_doc.selected <= 0)
        return;
    char *serialized = (char *)malloc(65536);
    if (!serialized)
        return;
    if (!ide_design_serialize_node(&s_design_doc, s_design_doc.selected,
                                   serialized, 65536))
    {
        free(serialized);
        return;
    }
    free(s_design_clipboard);
    s_design_clipboard = serialized;
    toast("Copied %s", s_design_doc.nodes[s_design_doc.selected].tag);
}

static void designer_paste_selected(void)
{
    if (!s_designer_visible || !s_design_clipboard)
        return;
    int index = ide_design_paste(&s_design_doc, s_design_clipboard);
    if (index >= 0)
    {
        s_design_doc.selected = index;
        designer_after_mutation();
        toast("Pasted %s", s_design_doc.nodes[index].tag);
    }
    else
    {
        toast("Cannot paste this element");
    }
}

static void ide_open_file(int index, bool force)
{
    if (index < 0 || index >= ide_file_count)
        return;
    if (!force && index == s_active_file)
        return;
    if (s_active_file >= 0 && s_designer_visible)
    {
        designer_apply_to_file();
        designer_set_visible(false);
    }
    if (s_active_file >= 0)
        save_active_state();

    bool was_open = ide_files[index].open;
    if (!was_open)
    {
        if (!ensure_tab_capacity())
            return;
        s_tabs[s_tab_count++] = index;
        ide_files[index].open = true;
    }

    s_active_file = index;
    IdeFile *file = &ide_files[index];

                                                                    
    markdown_preview_destroy();

                                                              
    if (s_editor && !strstr(file->name, ".md"))
        aroma_node_set_hidden(s_editor, false);

                                                                   
    if (strstr(file->name, ".md"))
    {
        s_outline_count = 0;
        s_outline_scroll = 0;
        s_outline_hover = -1;
        markdown_preview_create();
        draw_tabs();
        draw_breadcrumb();
        draw_outline();
        draw_sidebar();
        draw_status();
        if (s_panel_visible && s_panel_tab == 0)
            draw_panel();
        return;
    }

    aroma_code_editor_set_language(s_editor, file->language);
    aroma_code_editor_set_file_name(s_editor, file->path);
    aroma_code_editor_set_text(s_editor, file->text ? file->text : "");
    aroma_code_editor_set_diagnostics(s_editor, file->diagnostics,
                                      file->diagnostic_count);
    if (file->has_selection)
        aroma_code_editor_select(s_editor, file->anchor, file->cursor);
    else
        aroma_code_editor_set_cursor(s_editor, file->cursor);
    aroma_code_editor_set_scroll(s_editor, file->scroll_x, file->scroll_y);
    refresh_outline();

    if (!was_open)
        log_add("textDocument/didOpen", "%s  ·  %d diagnostics", file->path,
                file->diagnostic_count);
    else
        log_add("window/showDocument", "Revealed %s", file->path);

    draw_tabs();
    draw_breadcrumb();
    draw_sidebar();
    draw_status();
    if (s_panel_visible && s_panel_tab == 0)
        draw_panel();
    designer_sync_for_file();
    if (!s_designer_visible)
        aroma_code_editor_focus(s_editor);
}

static void refresh_workspace(void)
{
    if (s_designer_visible)
    {
        designer_apply_to_file();
        designer_set_visible(false);
    }
    save_active_state();
    char open_paths[EDITOR_TAB_LIMIT][IDE_PATH_MAX];
    int open_count = 0;
    char active_path[IDE_PATH_MAX];
    active_path[0] = '\0';
    if (s_active_file >= 0 && s_active_file < ide_file_count)
        snprintf(active_path, sizeof(active_path), "%s", ide_files[s_active_file].path);
    for (int i = 0; i < s_tab_count && open_count < EDITOR_TAB_LIMIT; i++)
    {
        int index = s_tabs[i];
        if (index >= 0 && index < ide_file_count)
            snprintf(open_paths[open_count++], IDE_PATH_MAX, "%s", ide_files[index].path);
    }

    if (!ide_workspace_reload())
    {
        toast("Could not refresh the workspace");
        return;
    }
    ide_lsp_refresh_all();
    s_tab_count = 0;
    s_active_file = -1;
    int wanted_active = -1;
    for (int i = 0; i < open_count; i++)
    {
        int index = ide_workspace_file_by_path(open_paths[i]);
        if (index < 0)
            continue;
        ide_files[index].open = true;
        s_tabs[s_tab_count++] = index;
        if (strcmp(open_paths[i], active_path) == 0)
            wanted_active = index;
    }
    tree_build();
    workspace_search(s_search_query);

    if (s_tab_count == 0 && ide_file_count > 0)
    {
        ide_files[0].open = true;
        s_tabs[s_tab_count++] = 0;
        wanted_active = 0;
    }
    if (wanted_active < 0 && s_tab_count > 0)
        wanted_active = s_tabs[0];
    if (wanted_active >= 0)
    {
        s_active_file = -1;
        ide_open_file(wanted_active, true);
    }
    else if (s_editor)
    {
        aroma_code_editor_set_text(s_editor, "");
        aroma_code_editor_set_file_name(s_editor, "");
    }
    log_add("workspace/didChangeWatchedFiles", "Scanned %d files in %s",
            ide_file_count, ide_workspace_root);
    draw_sidebar();
    draw_tabs();
    draw_breadcrumb();
    draw_panel();
    draw_status();
    toast("Workspace refreshed · %d files", ide_file_count);
}

static void close_tab(int index)
{
    if (index < 0 || index >= s_tab_count)
        return;
    if (s_tab_count <= 1)
    {
        toast("At least one editor must remain open");
        return;
    }
    if (s_tabs[index] == s_active_file)
        save_active_state();
    ide_files[s_tabs[index]].open = false;
    int was_active = s_tabs[index] == s_active_file;
    for (int i = index; i < s_tab_count - 1; i++)
        s_tabs[i] = s_tabs[i + 1];
    s_tab_count--;
    if (was_active)
    {
        int next = index > 0 ? index - 1 : 0;
        s_active_file = -1;
        ide_open_file(s_tabs[next], true);
    }
    else
    {
        draw_tabs();
        draw_sidebar();
        draw_status();
    }
}

static void set_panel_visible(bool visible)
{
    if (s_panel_visible == visible)
        return;
    s_panel_visible = visible;
    if (!visible)
    {
        if (s_editor)
            aroma_code_editor_focus(s_editor);
    }
    if (s_panel_detached)
    {
        if (s_panel_detached_canvas)
            aroma_node_set_hidden(s_panel_detached_canvas, !visible);
    }
    else
    {
        if (s_panel_canvas)
            aroma_node_set_hidden(s_panel_canvas, !visible);
        if (s_panel_bg)
            aroma_node_set_hidden(s_panel_bg, !visible);
        if (s_div_panel_top)
            aroma_node_set_hidden(s_div_panel_top, !visible);
    }

                                                                           
                                                       
    ide_layout_recalc();
    ide_sync_layout_nodes();
    if (s_editor)
        aroma_code_editor_set_bounds(s_editor, EDITOR_X, EDITOR_Y, editor_w,
                                     editor_h);
    markdown_preview_fit();
    if (s_editor_bg)
        aroma_node_invalidate(s_editor_bg);
    draw_all();

    if (visible)
        toast("Bottom panel restored");
    else
        toast("Bottom panel hidden - click the panel icon to restore");
    draw_status();
}

static bool goto_definition_target(int file_index, int line, int column,
                                   const char *symbol)
{
    if (file_index < 0 || file_index >= ide_file_count)
        return false;
    ide_open_file(file_index, false);
    aroma_code_editor_goto_line_col(s_editor, line, column);
    aroma_code_editor_focus(s_editor);
    log_add("textDocument/definition", "%s -> %s:%d:%d (resolved)",
            symbol && symbol[0] ? symbol : "symbol", ide_files[file_index].path,
            line, column);
    toast("Definition: %s  ·  %s:%d", symbol && symbol[0] ? symbol : "symbol",
          base_name(ide_files[file_index].path), line);
    draw_breadcrumb();
    draw_status();
    return true;
}

static int build_problem_list(void)
{
    s_problem_total = 0;
    for (int file_index = 0; file_index < ide_file_count; file_index++)
    {
        for (int diag_index = 0; diag_index < ide_files[file_index].diagnostic_count;
             diag_index++)
        {
            if (s_problem_total >= MAX_PROBLEMS)
                return s_problem_total;
            s_problem_file[s_problem_total] = file_index;
            s_problem_diag[s_problem_total] = diag_index;
            s_problem_total++;
        }
    }
    return s_problem_total;
}

static void count_diagnostics(int *errors, int *warnings, int *total)
{
    int err = 0;
    int warn = 0;
    int all = 0;
    for (int i = 0; i < ide_file_count; i++)
    {
        for (int j = 0; j < ide_files[i].diagnostic_count; j++)
        {
            AromaCodeDiagnosticSeverity severity = ide_files[i].diagnostics[j].severity;
            if (severity == AROMA_CODE_DIAGNOSTIC_ERROR)
                err++;
            else if (severity == AROMA_CODE_DIAGNOSTIC_WARNING)
                warn++;
            all++;
        }
    }
    if (errors)
        *errors = err;
    if (warnings)
        *warnings = warn;
    if (total)
        *total = all;
}

static void activate_problem(int problem_index)
{
    if (problem_index < 0 || problem_index >= s_problem_total)
        return;
    int file_index = s_problem_file[problem_index];
    int diag_index = s_problem_diag[problem_index];
    if (file_index < 0 || file_index >= ide_file_count ||
        diag_index < 0 || diag_index >= ide_files[file_index].diagnostic_count)
        return;
    AromaCodeDiagnostic *diag = &ide_files[file_index].diagnostics[diag_index];
    ide_open_file(file_index, false);
    aroma_code_editor_goto_line_col(s_editor, diag->line, diag->column);
    aroma_code_editor_focus(s_editor);
    log_add("window/showDocument", "Opened diagnostic in %s:%d:%d",
            ide_files[file_index].path, diag->line, diag->column);
    toast("%s  ·  %s:%d", diag->message, base_name(ide_files[file_index].path),
          diag->line);
}

static void open_search_hit(int index)
{
    if (index < 0 || index >= s_search_hit_count)
        return;
    const SearchHit *hit = &s_search_hits[index];
    if (hit->file < 0 || hit->file >= ide_file_count)
        return;
    ide_open_file(hit->file, false);
    aroma_code_editor_goto_line_col(s_editor, hit->line,
                                    hit->column > 0 ? hit->column : 1);
    aroma_code_editor_focus(s_editor);
    log_add("workspace/symbol", "Revealed %s:%d", ide_files[hit->file].path, hit->line);
    toast("Opened %s:%d", ide_files[hit->file].name, hit->line);
}
