#ifndef _C4_YML_PARSE_HPP_
#define _C4_YML_PARSE_HPP_

#ifndef _C4_YML_TREE_HPP_
#include "c4/yml/tree.hpp"
#endif

#ifndef _C4_YML_NODE_HPP_
#include "c4/yml/node.hpp"
#endif

#ifndef _C4_YML_DETAIL_STACK_HPP_
#include "c4/yml/detail/stack.hpp"
#endif

#include <stdarg.h>

namespace c4 {
namespace yml {

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

struct LineCol
{
    size_t offset, line, col;
    LineCol() : offset(), line(), col() {}
    LineCol(size_t o, size_t l, size_t c) : offset(o), line(l), col(c) {}
};

struct Location : public LineCol
{
    const char *name;
    operator bool () const { return name != nullptr || line != 0 || offset != 0; }

    Location() : LineCol(), name(nullptr) {}
    Location(const char *n, size_t b, size_t l, size_t c) : LineCol{b, l, c}, name(n) {}
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class Parser
{
public:

    Parser(Allocator const& a={});

    Tree parse(                         substr src) { return parse({}, src); }
    Tree parse(csubstr const& filename, substr src)
    {
        Tree t;
        t.reserve(_estimate_capacity(src));
        parse(filename, src, &t);
        return t;
    }

    void parse(                         substr src, Tree *t) { return parse({}, src, t); }
    void parse(csubstr const& filename, substr src, Tree *t)
    {
        NodeRef root(t);
        parse(filename, src, &root);
    }

    void parse(                         substr src, NodeRef * root) { return parse({}, src, root); }
    void parse(csubstr const& filename, substr src, NodeRef * root);

private:

    typedef enum {
        BLOCK_LITERAL, //!< keep newlines (|)
        BLOCK_FOLD     //!< replace newline with single space (>)
    } BlockStyle_e;

    typedef enum {
        CHOMP_CLIP,    //!< single newline at end (default)
        CHOMP_STRIP,   //!< no newline at end     (-)
        CHOMP_KEEP     //!< all newlines from end (+)
    } BlockChomp_e;

private:

    static size_t _estimate_capacity(csubstr src) { size_t c = _count_nlines(src); c = c >= 16 ? c : 16; return c; }

    void  _reset();

    bool  _finished_file() const;
    bool  _finished_line() const;

    csubstr _peek_next_line(size_t pos=npos) const;
    void  _scan_line();
    void  _next_line() { _line_ended(); }

    bool  _is_scalar_next(csubstr rem) const;
    bool  _is_scalar_next() const { return _is_scalar_next(m_state->line_contents.rem); }
    csubstr _scan_scalar();
    csubstr _scan_comment();
    csubstr _scan_quoted_scalar(const char q);
    csubstr _scan_block();
    csubstr _scan_ref();

    csubstr _filter_squot_scalar(substr s);
    csubstr _filter_dquot_scalar(substr s);
    csubstr _filter_plain_scalar(substr s, size_t indentation);
    csubstr _filter_block_scalar(substr s, BlockStyle_e style, BlockChomp_e chomp, size_t indentation);
    substr  _filter_whitespace(substr s, size_t indentation=0, bool leading_whitespace=true);

    void  _handle_finished_file();
    void  _handle_line();

    bool  _handle_indentation();

    bool  _handle_unk();
    bool  _handle_map_expl();
    bool  _handle_map_impl();
    bool  _handle_seq_expl();
    bool  _handle_seq_impl();
    bool  _handle_top();
    bool  _handle_anchors_and_refs();
    bool  _handle_types();

    void  _push_level(bool explicit_flow_chars = false);
    void  _pop_level();

    void  _start_unk(bool as_child=true);

    void  _start_map(bool as_child=true);
    void  _stop_map();

    void  _start_seq(bool as_child=true);
    void  _stop_seq();

    void  _start_doc(bool as_child=true);
    void  _stop_doc();

    void  _append_comment(csubstr const& cmt);
    NodeData* _append_val(csubstr const& val);
    NodeData* _append_key_val(csubstr const& val);
    bool  _rval_dash_start_or_continue_seq();

    void  _store_scalar(csubstr const& s);
    csubstr _consume_scalar();
    void  _move_scalar_from_top();

    void  _save_indentation(size_t behind = 0);

    void  _resolve_references();

private:

    static bool   _read_decimal(csubstr const& str, size_t *decimal);
    static size_t _count_nlines(csubstr src);

private:

    typedef enum {
        RTOP = 0x01 <<  0,   ///< reading at top level
        RUNK = 0x01 <<  1,   ///< reading an unknown: must determine whether scalar, map or seq
        RMAP = 0x01 <<  2,   ///< reading a map
        RSEQ = 0x01 <<  3,   ///< reading a seq
        EXPL = 0x01 <<  4,   ///< reading is inside explicit flow chars: [] or {}
        CPLX = 0x01 <<  5,   ///< reading a complex key
        RKEY = 0x01 <<  6,   ///< reading a scalar as key
        RVAL = 0x01 <<  7,   ///< reading a scalar as val
        RNXT = 0x01 <<  8,   ///< read next val or keyval
        SSCL = 0x01 <<  9,   ///< there's a scalar stored
    } State_e;

    struct LineContents
    {
        csubstr  full;        ///< the full line, including newlines on the right
        csubstr  stripped;    ///< the stripped line, excluding newlines on the right
        csubstr  rem;         ///< the stripped line remainder; initially starts at the first non-space character
        size_t indentation; ///< the number of spaces on the beginning of the line

        void reset(csubstr const& full_, csubstr const& stripped_)
        {
            full = full_;
            stripped = stripped_;
            rem = stripped_;
            // find the first column where the character is not a space
            indentation = full.first_not_of(' ');
        }
    };

    struct State
    {
        size_t       flags;
        size_t       level;
        size_t       node_id; // don't hold a pointer to the node as it will be relocated during tree resizes
        csubstr        scalar;

        Location     pos;
        LineContents line_contents;
        size_t       indref;

        State() { memset(this, 0, sizeof(*this)); }
        void reset(const char *file, size_t node_id_)
        {
            flags = RUNK|RTOP;
            level = 0;
            pos.name = file;
            pos.offset = 0;
            pos.line = 1;
            pos.col = 1;
            node_id = node_id_;
            scalar.clear();
            indref = 0;
        }
    };

    void _line_progressed(size_t ahead);
    void _line_ended();

    void _prepare_pop()
    {
        C4_ASSERT(m_stack.size() > 1);
        State const& curr = m_stack.top();
        State      & next = m_stack.top(1);
        next.pos = curr.pos;
        next.line_contents = curr.line_contents;
        next.scalar = curr.scalar;
    }

    inline bool _at_line_begin() const
    {
        return m_state->line_contents.rem.begin() == m_state->line_contents.full.begin();
    }
    inline bool _at_line_end() const
    {
        csubstr r = m_state->line_contents.rem;
        return r.empty() || r.begins_with(' ', r.len);
    }

    inline NodeData * node(State const* s) const { return m_tree->get(s->node_id); }
    inline NodeData * node(State const& s) const { return m_tree->get(s .node_id); }
    inline NodeData * node(size_t node_id) const { return m_tree->get(   node_id); }

    inline bool has_all(size_t f) const { return has_all(f, m_state); }
    inline bool has_any(size_t f) const { return has_any(f, m_state); }
    inline bool has_none(size_t f) const { return has_none(f, m_state); }

    inline bool has_all(size_t f, State const* s) const { return (s->flags & f) == f; }
    inline bool has_any(size_t f, State const* s) const { return (s->flags & f) != 0; }
    inline bool has_none(size_t f, State const* s) const { return (s->flags & f) == 0; }

    inline void set_flags(size_t f) { set_flags(f, m_state); }
    inline void add_flags(size_t on) { add_flags(on, m_state); }
    inline void addrem_flags(size_t on, size_t off) { addrem_flags(on, off, m_state); }
    inline void rem_flags(size_t off) { rem_flags(off, m_state); }

    void set_flags(size_t f, State * s);
    void add_flags(size_t on, State * s);
    void addrem_flags(size_t on, size_t off, State * s);
    void rem_flags(size_t off, State * s);

private:

#ifdef RYML_DBG
    void _dbg(const char *msg, ...) const;
#endif
    void _err(const char *msg, ...) const;
    int  _fmt_msg(char *buf, int buflen, const char *msg, va_list args) const;
    static int  _prfl(char *buf, int buflen, size_t v);

private:


    csubstr m_file;
     substr m_buf;

    size_t  m_root_id;
    Tree *  m_tree;

    detail::stack< State > m_stack;
    State * m_state;

    csubstr m_key_tag;
    csubstr m_val_tag;

    csubstr m_anchor;
    size_t  m_num_anchors;
    size_t  m_num_references;

};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

inline Tree parse(substr buf)
{
    Parser np;
    return np.parse(buf);
}
inline Tree parse(csubstr const& filename, substr buf)
{
    Parser np;
    return np.parse(filename, buf);
}

inline void parse(substr buf, Tree *t)
{
    Parser np;
    np.parse(buf, t);
}
inline void parse(csubstr const& filename, substr buf, Tree *t)
{
    Parser np;
    np.parse(filename, buf, t);
}

inline void parse(substr buf, NodeRef * root)
{
    Parser np;
    np.parse(buf, root);
}
inline void parse(csubstr const& filename, substr buf, NodeRef * root)
{
    Parser np;
    np.parse(filename, buf, root);
}

} // namespace yml
} // namespace c4

#endif /* _C4_YML_PARSE_HPP_ */
