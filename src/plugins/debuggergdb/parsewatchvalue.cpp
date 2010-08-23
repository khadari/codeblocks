/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU General Public License, version 3
 * http://www.gnu.org/licenses/gpl-3.0.html
 *
 * $Revision$
 * $Id$
 * $HeadURL$
 */

#include <sdk.h>

#ifndef CB_PRECOMP
#   include <wx/regex.h>
#endif

#include "parsewatchvalue.h"

struct Token
{
    enum Type
    {
        Undefined,
        OpenBrace,
        CloseBrace,
        Equal,
        String,
        Comma
    };

    Token() : type(Undefined)
    {
    }
    Token(int start_, int end_, Type type_) :
        start(start_),
        end(end_),
        type(type_)
    {
    }

    bool operator == (Token const &t) const
    {
        return start == t.start && end == t.end && type == t.type;
    }
    wxString ExtractString(wxString const &s) const
    {
        assert(end <= static_cast<int>(s.length()));
        return s.substr(start, end - start);
    }

    void Trim(wxString const &s)
    {
        while (start < static_cast<int>(s.length())
               && (s[start] == wxT(' ') || s[start] == wxT('\t') || s[start] == wxT('\n')))
            start++;
        while (end > 0
               && (s[end - 1] == wxT(' ') || s[end - 1] == wxT('\t') || s[end - 1] == wxT('\n')))
            end--;
    }

    int start, end;
    Type type;
};

bool GetNextToken(wxString const &str, int pos, Token &token)
{
    while (pos < static_cast<int>(str.length())
           && (str[pos] == _T(' ') || str[pos] == _T('\t') || str[pos] == _T('\n')))
        ++pos;

    if (pos >= static_cast<int>(str.length()))
        return false;

    token.start = -1;
    bool in_quote = false;
    int open_angle_braces = 0;

    switch (str[pos])
    {
    case _T('='):
        token = Token(pos, pos + 1, Token::Equal);
        return true;
    case _T(','):
        token = Token(pos, pos + 1, Token::Comma);
        return true;
    case _T('{'):
        token = Token(pos, pos + 1, Token::OpenBrace);
        return true;
    case _T('}'):
        token = Token(pos, pos + 1, Token::CloseBrace);
        return true;

    case _T('"'):
        in_quote = true;
        token.type = Token::String;
        token.start = pos;
        break;
    case _T('<'):
        token.type = Token::String;
        token.start = pos;
        open_angle_braces = 1;
    default:
        token.type = Token::String;
        token.start = pos;
    }
    ++pos;

    bool escape_next = false;
    while (pos < static_cast<int>(str.length()))
    {
        if (open_angle_braces == 0)
        {
            if ((str[pos] == _T(',') || str[pos] == _T('=') || str[pos] == _T('{')
                || str[pos] == _T('}'))
                && !in_quote)
            {
                token.end = pos;
                return true;
            }
            else if (str[pos] == _T('"') && in_quote && !escape_next)
            {
                token.end = pos + 1;
                return true;
            }
            else if (str[pos] == _T('\\'))
                escape_next = true;
            else
                escape_next = false;

            if (str[pos] == wxT('<'))
                open_angle_braces++;
        }
        else
        {
            if (str[pos] == wxT('<'))
                open_angle_braces++;
            else if (str[pos] == wxT('>'))
                --open_angle_braces;
        }
        ++pos;
    }

    if (in_quote)
    {
        token.end = -1;
        return false;
    }
    else
    {
        token.end = pos;
        return true;
    }
}

GDBWatch* AddChild(GDBWatch &parent, wxString const &full_value, Token &name)
{
    wxString const &str_name = name.ExtractString(full_value);
    cbWatch *old_child = parent.FindChild(str_name);
    GDBWatch *child;
    if (old_child)
        child = static_cast<GDBWatch*>(old_child);
    else
    {
        child = new GDBWatch(str_name);
        parent.AddChild(child);
    }
    child->MarkAsRemoved(false);
    return child;
}

GDBWatch* AddChild(GDBWatch &parent, wxString const &str_name)
{
    int index = parent.FindChildIndex(str_name);
    GDBWatch *child;
    if (index != -1)
        child = static_cast<GDBWatch*>(parent.GetChild(index));
    else
    {
        child = new GDBWatch(str_name);
        parent.AddChild(child);
    }
    child->MarkAsRemoved(false);
    return child;
}

bool ParseGDBWatchValue(GDBWatch &watch, wxString const &value, int &start, int length)
{
    watch.SetDebugValue(value);
    watch.MarkChildsAsRemoved();

    int position = start;
    Token token, token_name, token_value;
    bool skip_comma = false;
    bool last_was_closing_brace = false;
    int added_children = 0;
    int token_real_end = 0;
    while (GetNextToken(value, position, token))
    {
        token_real_end = token.end;
        token.Trim(value);
        const wxString &str = token.ExtractString(value);
        if (str.StartsWith(wxT("members of ")))
        {
            wxString::size_type pos = str.find(wxT('\n'));
            if (pos == wxString::npos)
                return false;
            else
            {
                if (str.find_last_of(wxT(':'), pos) == wxString::npos)
                    return false;
                token.start += pos + 2;
                token.Trim(value);
            }
        }

        switch (token.type)
        {
        case Token::String:
            if (token_name.type == Token::Undefined)
                token_name = token;
            else if (token_value.type == Token::Undefined)
                token_value = token;
            else
                return false;
            last_was_closing_brace = false;
            break;
        case Token::Equal:
            last_was_closing_brace = false;
            break;
        case Token::Comma:
            last_was_closing_brace = false;
            if (skip_comma)
                skip_comma = false;
            else
            {
                if (token_name.type != Token::Undefined)
                {
                    if (token_value.type != Token::Undefined)
                    {
                        GDBWatch *child = AddChild(watch, value, token_name);
                        child->SetValue(token_value.ExtractString(value));
                    }
                    else
                    {
                        int start = watch.IsArray() ? watch.GetArrayStart() : 0;
                        GDBWatch *child = AddChild(watch, wxString::Format(wxT("[%d]"), start + added_children));
                        child->SetValue(token_name.ExtractString(value));
                    }
                    token_name.type = token_value.type = Token::Undefined;
                    added_children++;
                }
                else
                    return false;
            }
            break;
        case Token::OpenBrace:
            {
                GDBWatch *child;
                if(token_name.type == Token::Undefined)
                {
                    int start = watch.IsArray() ? watch.GetArrayStart() : 0;
                    child = AddChild(watch, wxString::Format(wxT("[%d]"), start + added_children));
                }
                else
                    child = AddChild(watch, value, token_name);
                position = token_real_end;
                added_children++;

                if(!ParseGDBWatchValue(*child, value, position, 0))
                    return false;
                token_real_end = position;
                token_name.type = token_value.type = Token::Undefined;
                skip_comma = true;
                last_was_closing_brace = true;
            }
            break;
        case Token::CloseBrace:
            if (!last_was_closing_brace)
            {
                if (token_name.type != Token::Undefined)
                {
                    if (token_value.type != Token::Undefined)
                    {
                        GDBWatch *child = AddChild(watch, value, token_name);
                        child->SetValue(token_value.ExtractString(value));
                    }
                    else
                    {
                        int start = watch.IsArray() ? watch.GetArrayStart() : 0;
                        GDBWatch *child = AddChild(watch, wxString::Format(wxT("[%d]"), start + added_children));
                        child->SetValue(token_name.ExtractString(value));
                    }
                    token_name.type = token_value.type = Token::Undefined;
                    added_children++;
                }
                else
                    watch.SetValue(wxT(""));
            }

            start = token_real_end;
            return true;
        default:
            return false;
        }

        position = token_real_end;
        if (length > 0 && position >= start + length)
            break;
    }

    start = position + 1;
    if (token_name.type != Token::Undefined)
    {
        if (token_value.type != Token::Undefined)
        {
            GDBWatch *child = AddChild(watch, value, token_name);
            child->SetValue(token_value.ExtractString(value));
        }
        else
        {
            int start = watch.IsArray() ? watch.GetArrayStart() : 0;
            GDBWatch *child = AddChild(watch, wxString::Format(wxT("[%d]"), start + added_children));
            child->SetValue(token_name.ExtractString(value));
        }
    }

    return true;
}

wxString RemoveWarnings(wxString const &input)
{
    wxString::size_type pos = input.find(wxT('\n'));

    if (pos == wxString::npos)
        return input;

    wxString::size_type lastPos = 0;
    wxString result;

    while (pos != wxString::npos)
    {
        wxString const &line = input.substr(lastPos, pos - lastPos);

        if (!line.StartsWith(wxT("warning:")))
        {
            result += line;
            result += wxT('\n');
        }

        lastPos = pos + 1;
        pos = input.find(wxT('\n'), lastPos);
    }

    if (lastPos < input.length())
        result += input.substr(lastPos, input.length() - lastPos);

    return result;
}

bool ParseGDBWatchValue(GDBWatch &watch, wxString const &inputValue)
{
    if(inputValue.empty())
    {
        watch.SetValue(inputValue);
        return true;
    }

    wxString value = RemoveWarnings(inputValue);

    // Try to find the first brace.
    // If the watch is for a reference the brace is not at position = 0
    wxString::size_type start = value.find(wxT('{'));

    if (start != wxString::npos && value[value.length() - 1] == wxT('}'))
    {
        int t_start = start + 1;
        bool result = ParseGDBWatchValue(watch, value, t_start, value.length() - 2);
        if (result)
        {
            if (start > 0)
            {
                wxString referenceValue = value.substr(0, start);
                referenceValue.Trim(true);
                referenceValue.Trim(false);
                watch.SetValue(referenceValue);
            }
            watch.RemoveMarkedChildren();
        }
        return result;
    }
    else
    {
        watch.SetValue(value);
        watch.RemoveChildren();
        return true;
    }
    return false;
}

//
//    struct HWND__ * 0x7ffd8000
//
//    struct tagWNDCLASSEXA
//       +0x000 cbSize           : 0x7c8021b5
//       +0x004 style            : 0x7c802011
//       +0x008 lpfnWndProc      : 0x7c80b529     kernel32!GetModuleHandleA+0
//       +0x00c cbClsExtra       : 0
//       +0x010 cbWndExtra       : 2147319808
//       +0x014 hInstance        : 0x00400000
//       +0x018 hIcon            : 0x0012fe88
//       +0x01c hCursor          : 0x0040a104
//       +0x020 hbrBackground    : 0x689fa962
//       +0x024 lpszMenuName     : 0x004028ae  "???"
//       +0x028 lpszClassName    : 0x0040aa30  "CodeBlocksWindowsApp"
//       +0x02c hIconSm          : (null)
//
//    char * 0x0040aa30
//     "CodeBlocksWindowsApp"
//
//    char [16] 0x0012fef8
//    116 't'
//
//    int [10] 0x0012fee8
//    0

bool ParseCDBWatchValue(GDBWatch &watch, wxString const &value)
{
    wxArrayString lines = GetArrayFromString(value, wxT('\n'));
    watch.SetDebugValue(value);
    watch.MarkChildsAsRemoved();

    if (lines.GetCount() == 0)
        return false;

    static wxRegEx unexpected_error(wxT("^Unexpected token '.+'$"));
    static wxRegEx resolve_error(wxT("^Couldn't resolve error at '.+'$"));

    // search for errors
    for (unsigned ii = 0; ii < lines.GetCount(); ++ii)
    {
        if (unexpected_error.Matches(lines[ii])
            || resolve_error.Matches(lines[ii])
            || lines[ii] == wxT("No pointer for operator* '<EOL>'"))
        {
            watch.SetValue(lines[ii]);
            return true;
        }
    }

    if (lines.GetCount() == 1)
    {
        wxArrayString tokens = GetArrayFromString(lines[0], wxT(' '));
        if (tokens.GetCount() < 2)
            return false;

        int type_token = 0;
        if (tokens[0] == wxT("class") || tokens[0] == wxT("class"))
            type_token = 1;

        if (static_cast<int>(tokens.GetCount()) < type_token + 2)
            return false;

        int value_start = type_token + 1;
        if (tokens[type_token + 1] == wxT('*'))
        {
            watch.SetType(tokens[type_token] + tokens[type_token + 1]);
            value_start++;
        }
        else
            watch.SetType(tokens[type_token]);

        if(value_start >= static_cast<int>(tokens.GetCount()))
            return false;

        watch.SetValue(tokens[value_start]);
        watch.RemoveMarkedChildren();
        return true;
    }
    else
    {
        wxArrayString tokens = GetArrayFromString(lines[0], wxT(' '));

        if (tokens.GetCount() < 2)
            return false;

        bool set_type = true;
        if (tokens.GetCount() > 2)
        {
            if (tokens[0] == wxT("struct") || tokens[0] == wxT("class"))
            {
                if (tokens[2] == wxT('*') || tokens[2].StartsWith(wxT("[")))
                {
                    watch.SetType(tokens[1] + tokens[2]);
                    set_type = false;
                }
            }
            else
            {
                if (tokens[1] == wxT('*') || tokens[1].StartsWith(wxT("[")))
                {

                    watch.SetType(tokens[0] + tokens[1]);
                    watch.SetValue(lines[1]);
                    return true;
                }
            }
        }

        if (set_type)
            watch.SetType(tokens[1]);

        static wxRegEx class_line(wxT("[ \\t]*\\+(0x[0-9a-f]+)[ \\t]([a-zA-Z0-9_]+)[ \\t]+:[ \\t]+(.+)"));
        if (!class_line.IsValid())
        {
            int *p = NULL;
            *p = 0;
        }
        else
        {
            if (!class_line.Matches(wxT("   +0x000 a                : 10")))
            {
                int *p = NULL;
                *p = 0;
            }
        }

        for (unsigned ii = 1; ii < lines.GetCount(); ++ii)
        {
            if (class_line.Matches(lines[ii]))
            {
                GDBWatch *w = AddChild(watch, class_line.GetMatch(lines[ii], 2));
                w->SetValue(class_line.GetMatch(lines[ii], 3));
                w->SetDebugValue(lines[ii]);
            }
        }
        watch.RemoveMarkedChildren();
        return true;
    }

    return false;
}