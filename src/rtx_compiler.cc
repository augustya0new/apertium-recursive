#include <rtx_compiler.h>
#include <apertium/string_utils.h>
#include <apertium/utf_converter.h>

using namespace std;

wstring const
RTXCompiler::ANY_TAG = L"<ANY_TAG>";

wstring const
RTXCompiler::ANY_CHAR = L"<ANY_CHAR>";

RTXCompiler::RTXCompiler()
{
  longestPattern = 0;
  currentRule = NULL;
  currentChunk = NULL;
  currentChoice = NULL;
  errorsAreSyntax = true;
  inOutputRule = false;
  parserIsInChunk = false;
  inMacro = false;
  PB.starCanBeEmpty = true;
  fallbackRule = true;
  summarizing = false;
}

wstring const
RTXCompiler::SPECIAL_CHARS = L"!@$%()={}[]|/:;<>,.→";

void
RTXCompiler::die(wstring message)
{
  if(errorsAreSyntax)
  {
    wcerr << L"Syntax error on line " << currentLine << L" of ";
  }
  else
  {
    wcerr << L"Error in rule beginning on line " << currentRule->line << L" of ";
  }
  wcerr << UtfConverter::fromUtf8(sourceFile) << L": " << message << endl;
  if(errorsAreSyntax && !source.eof())
  {
    wstring arr = wstring(recentlyRead.size()-2, L' ');
    while(!source.eof() && source.peek() != L'\n')
    {
      recentlyRead += source.get();
    }
    wcerr << recentlyRead << endl;
    wcerr << arr << L"^^^" << endl;
  }
  exit(EXIT_FAILURE);
}

void
RTXCompiler::eatSpaces()
{
  wchar_t c;
  bool inComment = false;
  while(!source.eof())
  {
    c = source.peek();
    if(c == L'\n')
    {
      source.get();
      inComment = false;
      currentLine++;
      recentlyRead.clear();
    }
    else if(inComment)
    {
      recentlyRead += source.get();
    }
    else if(isspace(c))
    {
      recentlyRead += source.get();
    }
    else if(c == L'!')
    {
      recentlyRead += source.get();
      inComment = true;
    }
    else
    {
      break;
    }
  }
}

wstring
RTXCompiler::nextTokenNoSpace()
{
  if(source.eof())
  {
    die(L"Unexpected end of file");
  }
  wchar_t c = source.get();
  wchar_t next = source.peek();
  wstring ret;
  if(c == L'→')
  {
    recentlyRead += c;
    ret = L"->";
  }
  else if(SPECIAL_CHARS.find(c) != string::npos)
  {
    ret = wstring(1, c);
    recentlyRead += c;
  }
  else if(c == L'-' && next == L'>')
  {
    next = source.get();
    ret = wstring(1, c) + wstring(1, next);
    recentlyRead += ret;
  }
  else if(isspace(c))
  {
    die(L"unexpected space");
  }
  else if(c == L'!')
  {
    die(L"unexpected comment");
  }
  else if(c == L'"')
  {
    next = source.get();
    while(!source.eof() && next != L'"')
    {
      if(next == L'\\') next = source.get();
      ret += next;
      if(source.eof()) die(L"Unexpected end of file.");
      next = source.get();
    }
  }
  else
  {
    ret = wstring(1, c);
    while(!source.eof())
    {
      c = source.peek();
      if(c == L'\\')
      {
        ret += source.get();
        ret += source.get();
      }
      else if(SPECIAL_CHARS.find(c) == string::npos && !isspace(c))
      {
        ret += wstring(1, source.get());
      }
      else
      {
        break;
      }
    }
    recentlyRead += ret;
  }
  return ret;
}

bool
RTXCompiler::isNextToken(wchar_t c)
{
  if(source.peek() == c)
  {
    recentlyRead += source.get();
    return true;
  }
  return false;
}

wstring
RTXCompiler::nextToken(wstring check1 = L"", wstring check2 = L"")
{
  eatSpaces();
  wstring tok = nextTokenNoSpace();
  if(tok == check1 || tok == check2 || (check1 == L"" && check2 == L""))
  {
  }
  else if(check1 != L"" && check2 != L"")
  {
    die(L"expected '" + check1 + L"' or '" + check2 + L"', found '" + tok + L"'");
  }
  else if(check1 != L"")
  {
    die(L"expected '" + check1 + L"', found '" + tok + L"'");
  }
  else
  {
    die(L"expected '" + check2 + L"', found '" + tok + L"'");
  }
  return tok;
}

wstring
RTXCompiler::parseIdent(bool prespace = false)
{
  if(prespace)
  {
    eatSpaces();
  }
  wstring ret = nextTokenNoSpace();
  if(ret == L"->" || (ret.size() == 1 && SPECIAL_CHARS.find(ret[0]) != string::npos))
  {
    die(L"expected identifier, found '" + ret + L"'");
  }
  return ret;
}

unsigned int
RTXCompiler::parseInt()
{
  wstring ret;
  while(isdigit(source.peek()))
  {
    ret += source.get();
  }
  recentlyRead += ret;
  return stoul(ret);
}

float
RTXCompiler::parseWeight()
{
  wstring ret;
  while(isdigit(source.peek()) || source.peek() == L'.')
  {
    ret += source.get();
  }
  recentlyRead += ret;
  try
  {
    wstring::size_type loc;
    float r = stof(ret, &loc);
    if(loc == ret.size())
    {
      return r;
    }
    else
    {
      die(L"unable to parse weight: " + ret);
    }
  }
  catch(const invalid_argument& ia)
  {
    die(L"unable to parse weight: " + ret);
  }
}

void
RTXCompiler::parseRule()
{
  wstring firstLabel = parseIdent();
  wstring next = nextToken();
  if(next == L":")
  {
    parseOutputRule(firstLabel);
  }
  else if(next == L">")
  {
    parseRetagRule(firstLabel);
  }
  else if(next == L"=")
  {
    parseAttrRule(firstLabel);
  }
  else
  {
    parseReduceRule(firstLabel, next);
  }
}

void
RTXCompiler::parseOutputRule(wstring pattern)
{
  nodeIsSurface[pattern] = !isNextToken(L':');
  eatSpaces();
  vector<wstring> output;
  if(source.peek() == L'(')
  {
    inMacro = true;
    parserIsInChunk = true;
    currentRule = new Rule;
    currentRule->pattern.push_back(vector<wstring>(2, L"blah"));
    parseOutputCond();
    macros[pattern] = currentChoice;
    currentChoice = NULL;
    output.push_back(L"macro");
    currentRule = NULL;
    parserIsInChunk = false;
    inMacro = false;
    nextToken(L";");
  }
  else
  {
    wstring cur;
    while(!source.eof())
    {
      cur = nextToken();
      if(cur == L"<")
      {
        cur = cur + parseIdent();
        cur += nextToken(L">");
      }
      output.push_back(cur);
      if(nextToken(L".", L";") == L";")
      {
        break;
      }
    }
    if(output.size() == 0)
    {
      die(L"empty tag order rule");
    }
  }
  outputRules[pattern] = output;
}

void
RTXCompiler::parseRetagRule(wstring srcTag)
{
  wstring destTag = parseIdent(true);
  nextToken(L":");
  vector<pair<wstring, wstring>> rule;
  rule.push_back(pair<wstring, wstring>(srcTag, destTag));
  while(!source.eof())
  {
    eatSpaces();
    bool list = isNextToken(L'[');
    wstring cs = parseIdent(true);
    if(list)
    {
      nextToken(L"]");
      cs = L"[]" + cs;
    }
    wstring cd = parseIdent(true);
    rule.push_back(pair<wstring, wstring>(cs, cd));
    if(nextToken(L";", L",") == L";")
    {
      break;
    }
  }
  bool found = false;
  for(auto other : retagRules)
  {
    if(other[0].first == srcTag && other[0].second == destTag)
    {
      found = true;
      wcerr << "Warning: Tag-rewrite rule '" << srcTag << "' > '" << destTag << "' is defined multiple times. Mappings in earlier definition may be overwritten." << endl;
      other.insert(other.begin()+1, rule.begin()+1, rule.end());
      break;
    }
  }
  if(!found)
  {
    retagRules.push_back(rule);
    if(altAttrs.find(destTag) == altAttrs.end())
    {
      altAttrs[destTag].push_back(destTag);
    }
    altAttrs[destTag].push_back(srcTag);
  }
}

void
RTXCompiler::parseAttrRule(wstring categoryName)
{
  if(collections.find(categoryName) != collections.end())
  {
    die(L"redefinition of category " + categoryName);
  }
  eatSpaces();
  if(isNextToken(L'('))
  {
    wstring undef = parseIdent(true);
    wstring def = parseIdent(true);
    attrDefaults[categoryName] = make_pair(undef, def);
    nextToken(L")");
  }
  vector<wstring> members;
  vector<wstring> noOver;
  while(true)
  {
    eatSpaces();
    if(isNextToken(L';'))
    {
      break;
    }
    if(isNextToken(L'['))
    {
      wstring other = parseIdent(true);
      if(collections.find(other) == collections.end())
      {
        die(L"Use of category '" + other + L"' in set arithmetic before definition.");
      }
      vector<wstring> otherstuff = collections[other];
      for(unsigned int i = 0; i < otherstuff.size(); i++)
      {
        members.push_back(otherstuff[i]);
      }
      otherstuff = noOverwrite[other];
      for(unsigned int i = 0; i < otherstuff.size(); i++)
      {
        noOver.push_back(otherstuff[i]);
      }
      nextToken(L"]");
    }
    else if(isNextToken(L'@'))
    {
      wstring next = parseIdent();
      members.push_back(next);
      noOver.push_back(next);
    }
    else
    {
      members.push_back(parseIdent());
    }
  }
  if(members.size() == 0)
  {
    die(L"empty attribute list");
  }
  collections.insert(pair<wstring, vector<wstring>>(categoryName, members));
  noOverwrite.insert(pair<wstring, vector<wstring>>(categoryName, noOver));
  if(noOver.size() > 0)
  {
    for(unsigned int i = 0; i < noOver.size(); i++)
    {
      noOver[i] = L"<" + noOver[i] + L">";
    }
  }
  collections.insert(make_pair(categoryName + L" over", noOver));
}

RTXCompiler::Clip*
RTXCompiler::parseClip(int src = -2)
{
  Clip* ret = new Clip;
  eatSpaces();
  if(src != -2 && src != -3)
  {
    ret->src = src;
  }
  else if(isdigit(source.peek()))
  {
    ret->src = parseInt();
    nextToken(L".");
  }
  else if(isNextToken(L'$'))
  {
    ret->src = -1;
  }
  else
  {
    ret->src = 0;
  }
  if(inMacro && !(ret->src == 0 || ret->src == 1))
  {
    die(L"Macros can only access their single argument.");
  }
  else if(src == -2 && ret->src > (int)currentRule->pattern.size())
  {
    die(L"Clip source is out of bounds (position " + to_wstring(ret->src) + L" requested, but rule has only " + to_wstring(currentRule->pattern.size()) + L" elements in its pattern).");
  }
  ret->part = (src == -3) ? nextToken() : parseIdent();
  if(isNextToken(L'/'))
  {
    if(ret->src == 0)
    {
      die(L"literal value cannot have a side");
    }
    else if(ret->src == -1)
    {
      die(L"variable cannot have a side");
    }
    ret->side = parseIdent();
  }
  if(isNextToken(L'>'))
  {
    if(ret->src == 0)
    {
      die(L"literal value cannot be rewritten");
    }
    else if(ret->src == -1)
    {
      die(L"variable cannot be rewritten");
    }
    ret->rewrite = parseIdent();
  }
  return ret;
}

wchar_t
RTXCompiler::lookupOperator(wstring op)
{
  wstring key = StringUtils::tolower(op);
  key = StringUtils::substitute(key, L"-", L"");
  key = StringUtils::substitute(key, L"_", L"");
  for(unsigned int i = 0; i < OPERATORS.size(); i++)
  {
    if(key == OPERATORS[i].first)
    {
      return OPERATORS[i].second;
    }
  }
  return 0;
}

RTXCompiler::Cond*
RTXCompiler::parseCond()
{
  // TODO: if anyone wants (1.lem = and) things will go wrong
  // maybe something with (1.lem = "and") ?
  nextToken(L"(");
  eatSpaces();
  vector<Cond*> parts;
  while(!source.eof() && source.peek() != L')')
  {
    if(source.peek() == L'(')
    {
      parts.push_back(parseCond());
    }
    else
    {
      Cond* p = new Cond;
      p->op = 0;
      p->val = parseClip(-3);
      parts.push_back(p);
    }
    eatSpaces();
  }
  nextToken(L")");
  if(parts.size() == 0) die(L"Empty conditional.");
  vector<pair<bool, Cond*>> denot;
  bool negated = false;
  for(unsigned int i = 0; i < parts.size(); i++)
  {
    if(i != parts.size() - 1 && parts[i]->op == 0
       && parts[i]->val->src == 0)
    {
      wchar_t op = lookupOperator(parts[i]->val->part);
      if(op == NOT)
      {
        negated = !negated;
        continue;
      }
    }
    denot.push_back(make_pair(negated, parts[i]));
    negated = false;
  }
  vector<pair<bool, Cond*>> destring;
  for(unsigned int i = 0; i < denot.size(); i++)
  {
    if(i != 0 && i != denot.size() - 1 && denot[i].second->op == 0
       && denot[i].second->val->src == 0)
    {
      wchar_t op = lookupOperator(denot[i].second->val->part);
      if(op != 0 && op != AND && op != OR && op != NOT)
      {
        if(destring.back().second->op == 0 && denot[i+1].second->op == 0)
        {
          if(destring.back().first || denot[i+1].first)
          {
            die(L"Cannot negate string (I can't parse 'not a = b', use 'not (a = b)' or 'a not = b' instead).");
          }
          denot[i].second->left = destring.back().second;
          denot[i].second->right = denot[i+1].second;
          destring.pop_back();
          denot[i].second->op = op;
          denot[i].second->val = NULL;
          destring.push_back(denot[i]);
          i++;
          continue;
        }
      }
    }
    destring.push_back(denot[i]);
  }
  Cond* ret;
  if(destring[0].first)
  {
    ret = new Cond;
    ret->op = NOT;
    ret->right = destring[0].second;
  }
  else ret = destring[0].second;
  if(destring.size() % 2 == 0) die(L"ANDs, ORs, and conditions don't come out evenly.");
  for(unsigned int i = 1; i < destring.size(); i += 2)
  {
    if(destring[i].second->op != 0) die(L"Expected operator, found condition.");
    if(destring[i].second->val->src != 0) die(L"Expected operator, found clip.");
    wchar_t op = lookupOperator(destring[i].second->val->part);
    if(op == 0) die(L"Unknown operator '" + destring[i].second->val->part + L"'.");
    Cond* temp = ret;
    ret = new Cond;
    ret->left = temp;
    ret->op = op;
    if(destring[i+1].first)
    {
      ret->right = new Cond;
      ret->right->right = destring[i+1].second;
      ret->op = NOT;
    }
    else ret->right = destring[i+1].second;
    if(destring[i].first)
    {
      temp = ret;
      ret = new Cond;
      ret->right = temp;
      ret->op = NOT;
    }
  }
  return ret;
}

void
RTXCompiler::parsePatternElement(Rule* rule)
{
  vector<wstring> pat;
  if(isNextToken(L'%'))
  {
    rule->grab_all = rule->pattern.size()+1;
  }
  wstring t1 = nextToken();
  if(t1 == L"$")
  {
    t1 += parseIdent();
  }
  if(isNextToken(L'@'))
  {
    pat.push_back(t1);
    pat.push_back(parseIdent());
  }
  else if(t1[0] == L'$')
  {
    die(L"first tag in pattern element must be literal");
  }
  else
  {
    pat.push_back(L"");
    pat.push_back(t1);
  }
  while(!source.eof())
  {
    if(!isNextToken(L'.'))
    {
      break;
    }
    wstring cur = nextToken();
    if(cur == L"$")
    {
      Clip* cl = parseClip(rule->pattern.size()+1);
      if(rule->vars.find(cl->part) != rule->vars.end())
      {
        die(L"rule has multiple sources for attribute " + cl->part);
      }
      rule->vars[cl->part] = cl;
    }
    else if(cur == L"[")
    {
      pat.push_back(L"[" + parseIdent() + L"]");
      nextToken(L"]");
    }
    else
    {
      pat.push_back(cur);
    }
  }
  rule->pattern.push_back(pat);
  eatSpaces();
}

void
RTXCompiler::parseOutputElement()
{
  OutputChunk* ret = new OutputChunk;
  ret->conjoined = isNextToken(L'+');
  ret->nextConjoined = false;
  if(ret->conjoined)
  {
    if(currentChunk == NULL)
    {
      die(L"Cannot conjoin from within if statement.");
    }
    if(currentChunk->children.size() == 0)
    {
      die(L"Cannot conjoin first element.");
    }
    if(currentChunk->children.back()->conds.size() > 0)
    {
      die(L"Cannot conjoin to something in an if statement.");
    }
    if(currentChunk->children.back()->chunks.size() == 0)
    {
      die(L"Cannot conjoin inside and outside of if statement and cannot conjoin first element.");
    }
    if(currentChunk->children.back()->chunks[0]->mode == L"_")
    {
      die(L"Cannot conjoin to a blank.");
    }
    eatSpaces();
    currentChunk->children.back()->chunks[0]->nextConjoined = true;
  }
  ret->getall = isNextToken(L'%');
  if(source.peek() == L'_')
  {
    if(ret->getall)
    {
      die(L"% cannot be used on blanks");
    }
    ret->mode = L"_";
    recentlyRead += source.get();
    if(isdigit(source.peek()))
    {
      ret->pos = parseInt();
      if(currentRule->pattern.size() == 1)
      {
        die(L"Cannot output indexed blank because pattern is one element long and thus does not include blanks.");
      }
      if(ret->pos < 1 || ret->pos >= currentRule->pattern.size())
      {
        die(L"Position index of blank out of bounds, expected an integer from 1 to " + to_wstring(currentRule->pattern.size()-1) + L".");
      }
    }
    else
    {
      ret->pos = 0;
    }
  }
  else if(isdigit(source.peek()))
  {
    ret->mode = L"#";
    ret->pos = parseInt();
    if(ret->pos == 0)
    {
      die(L"There is no position 0.");
    }
    else if(ret->pos > currentRule->pattern.size())
    {
      die(L"There are only " + to_wstring(currentRule->pattern.size()) + L" elements in the pattern.");
    }
    if(source.peek() == L'[')
    {
      nextToken(L"[");
      ret->pattern = parseIdent();
      nextToken(L"]");
    }
    else
    {
      ret->pattern = currentRule->pattern[ret->pos-1][1];
    }
  }
  else if(isNextToken(L'*'))
  {
    if(source.peek() != L'[')
    {
      die(L"No macro name specified.");
    }
    nextToken(L"[");
    ret->pattern = parseIdent(true);
    nextToken(L"]");
    ret->pos = 0;
    ret->mode = L"#";
  }
  else
  {
    ret->lemma = parseIdent();
    ret->pos = 0;
    wstring mode = nextToken(L"@", L"[");
    if(mode == L"@")
    {
      if(ret->getall)
      {
        die(L"% not supported on output literals with @. Use %lemma[pos].");
      }
      ret->mode = L"@";
      while(true)
      {
        wstring cur = nextToken();
        wstring var = to_wstring(ret->tags.size());
        ret->tags.push_back(var);
        Clip* cl = new Clip;
        if(cur == L"$")
        {
          cl->src = -1;
          cl->part = parseIdent();
        }
        else if(cur == L"[")
        {
          cl = parseClip();
          nextToken(L"]");
        }
        else
        {
          cl->src = 0;
          cl->part = cur;
        }
        ret->vars[var] = cl;
        if(!isNextToken(L'.'))
        {
          break;
        }
      }
    }
    else
    {
      ret->mode = L"#@";
      ret->pattern = parseIdent(true);
      nextToken(L"]");
    }
  }
  if(isNextToken(L'('))
  {
    while(!source.eof() && source.peek() != L')')
    {
      eatSpaces();
      wstring var = parseIdent();
      nextToken(L"=");
      eatSpaces();
      Clip* cl = parseClip();
      if(cl->part == L"_")
      {
        cl->part = L"";
      }
      ret->vars[var] = cl;
      if(nextToken(L",", L")") == L")")
      {
        break;
      }
    }
  }
  if(currentChoice != NULL)
  {
    currentChoice->chunks.push_back(ret);
    currentChoice->nest.push_back(NULL);
  }
  else
  {
    OutputChoice* temp = new OutputChoice;
    temp->chunks.push_back(ret);
    temp->nest.push_back(NULL);
    if(currentChunk == NULL)
    {
      currentRule->output.push_back(temp);
    }
    else
    {
      currentChunk->children.push_back(temp);
    }
  }
  eatSpaces();
}

void
RTXCompiler::parseOutputCond()
{
  nextToken(L"(");
  OutputChoice* choicewas = currentChoice;
  OutputChunk* chunkwas = currentChunk;
  OutputChoice* ret = new OutputChoice;
  currentChoice = ret;
  currentChunk = NULL;
  while(true)
  {
    wstring mode = StringUtils::tolower(nextToken());
    mode = StringUtils::substitute(mode, L"-", L"");
    mode = StringUtils::substitute(mode, L"_", L"");
    if(ret->conds.size() == 0 && mode != L"if" && mode != L"always")
    {
      die(L"If statement must begin with 'if'.");
    }
    if(ret->conds.size() > 0 && mode == L"always")
    {
      die(L"Always clause must be only clause.");
    }
    if(mode == L"if" || mode == L"elif" || mode == L"elseif")
    {
      ret->conds.push_back(parseCond());
    }
    else if(mode == L")")
    {
      break;
    }
    else if(mode != L"else" && mode != L"otherwise" && mode != L"always")
    {
      die(L"Unknown statement: '" + mode + L"'.");
    }
    eatSpaces();
    if(source.peek() == L'(')
    {
      parseOutputCond();
      ret->chunks.push_back(NULL);
    }
    else if(source.peek() == L'{')
    {
      if(parserIsInChunk)
      {
        die(L"Nested chunks are currently not allowed.");
      }
      ret->nest.push_back(NULL);
      parseOutputChunk();
    }
    else if(source.peek() == L'[')
    {
      ret->nest.push_back(NULL);
      parseOutputChunk();
    }
    else
    {
      if(!parserIsInChunk)
      {
        die(L"Conditional non-chunk output current not possible.");
      }
      parseOutputElement();
      ret->nest.push_back(NULL);
    }
    if(mode == L"else" || mode == L"otherwise" || mode == L"always")
    {
      nextToken(L")");
      break;
    }
  }
  currentChunk = chunkwas;
  currentChoice = choicewas;
  if(ret->chunks.size() == 0)
  {
    die(L"If statement cannot be empty.");
  }
  if(ret->conds.size() == ret->nest.size())
  {
    //die(L"If statement has no else clause and thus could produce no output.");
    ret->nest.push_back(NULL);
    OutputChunk* temp = new OutputChunk;
    temp->mode = L"[]";
    temp->pos = 0;
    ret->chunks.push_back(temp);
  }
  eatSpaces();
  if(currentChoice != NULL)
  {
    currentChoice->nest.push_back(ret);
    currentChoice->chunks.push_back(NULL);
  }
  else if(currentChunk != NULL)
  {
    currentChunk->children.push_back(ret);
  }
  else if(inMacro)
  {
    currentChoice = ret;
  }
  else
  {
    currentRule->output.push_back(ret);
  }
}

void
RTXCompiler::parseOutputChunk()
{
  wchar_t end;
  OutputChunk* ch = new OutputChunk;
  if(nextToken(L"{", L"[") == L"{")
  {
    parserIsInChunk = true;
    ch->mode = L"{}";
    end = L'}';
  }
  else
  {
    if(!parserIsInChunk)
    {
      die(L"Output grouping with [] only valid inside chunks.");
    }
    ch->mode == L"[]";
    end = L']';
  }
  eatSpaces();
  OutputChunk* chunkwas = currentChunk;
  OutputChoice* choicewas = currentChoice;
  currentChunk = ch;
  currentChoice = NULL;
  ch->pos = 0;
  while(source.peek() != end)
  {
    if(source.peek() == L'(')
    {
      parseOutputCond();
    }
    else
    {
      parseOutputElement();
    }
  }
  nextToken(wstring(1, end));
  if(end == L'}') parserIsInChunk = false;
  eatSpaces();
  currentChunk = chunkwas;
  currentChoice = choicewas;
  OutputChoice* ret = new OutputChoice;
  ret->chunks.push_back(ch);
  ret->nest.push_back(NULL);
  if(currentChoice != NULL)
  {
    currentChoice->chunks.push_back(ch);
  }
  else if(currentChunk != NULL)
  {
    currentChunk->children.push_back(ret);
  }
  else
  {
    currentRule->output.push_back(ret);
  }
}

void
RTXCompiler::parseReduceRule(wstring output, wstring next)
{
  vector<wstring> outNodes;
  outNodes.push_back(output);
  if(next != L"->")
  {
    wstring cur = next;
    while(cur != L"->")
    {
      if(SPECIAL_CHARS.find(cur) != wstring::npos)
      {
        die(L"Chunk names must be identifiers. (I think I'm parsing a reduction rule.)");
      }
      outNodes.push_back(cur);
      cur = nextToken();
    }
  }
  Rule* rule;
  while(true)
  {
    rule = new Rule();
    currentRule = rule;
    rule->grab_all = -1;
    rule->result = outNodes;
    eatSpaces();
    rule->line = currentLine;
    if(isdigit(source.peek()))
    {
      rule->weight = parseWeight();
      nextToken(L":");
      eatSpaces();
    }
    else
    {
      rule->weight = 0;
    }
    while(!source.eof() && source.peek() != L'{' && source.peek() != L'[' && source.peek() != L'(' && source.peek() != L'?')
    {
      parsePatternElement(rule);
    }
    if(rule->pattern.size() == 0)
    {
      die(L"empty pattern");
    }
    eatSpaces();
    if(isNextToken(L'?'))
    {
      rule->cond = parseCond();
      eatSpaces();
    }
    if(isNextToken(L'['))
    {
      while(!source.eof())
      {
        nextToken(L"$");
        wstring var = parseIdent();
        if(rule->vars.find(var) != rule->vars.end())
        {
          die(L"rule has multiple sources for attribute " + var);
        }
        nextToken(L"=");
        rule->vars[var] = parseClip();
        if(nextToken(L",", L"]") == L"]")
        {
          break;
        }
      }
      eatSpaces();
    }
    if(rule->result.size() > 1)
    {
      nextToken(L"{");
    }
    int chunk_count = 0;
    while(chunk_count < rule->result.size())
    {
      eatSpaces();
      if(source.eof()) die(L"Unexpected end of file.");
      switch(source.peek())
      {
        case L'(':
          parseOutputCond();
          chunk_count++;
          break;
        case L'{':
          parseOutputChunk();
          chunk_count++;
          break;
        case L'_':
          parseOutputElement();
          break;
        case L'}':
          if(rule->result.size() == 1)
          {
            die(L"Unexpected } in output pattern.");
          }
          else if(chunk_count < rule->result.size())
          {
            die(L"Output pattern does not have enough chunks.");
          }
          break;
        default:
          parseOutputElement();
          chunk_count++;
          break;
      }
    }
    if(rule->result.size() > 1)
    {
      nextToken(L"}");
    }
    reductionRules.push_back(rule);
    if(nextToken(L"|", L";") == L";")
    {
      break;
    }
  }
}

void
RTXCompiler::processRetagRules()
{
  for(auto rule : retagRules)
  {
    map<wstring, vector<wstring>> vals;
    wstring src = rule[0].first;
    wstring dest = rule[0].second;
    if(!PB.isAttrDefined(src) && collections.find(src) == collections.end())
    {
      wcerr << L"Warning: Source category for tag-rewrite rule '" << src << "' > '" << dest << "' is undefined." << endl;
      continue;
    }
    if(!PB.isAttrDefined(dest) && collections.find(dest) == collections.end())
    {
      wcerr << L"Warning: Destination category for tag-rewrite rule '" << src << "' > '" << dest << "' is undefined." << endl;
      continue;
    }
    if(collections.find(src) == collections.end() || collections.find(dest) == collections.end()) continue;
    for(unsigned int i = 1; i < rule.size(); i++)
    {
      if(rule[i].first[0] == L'[')
      {
        wstring cat = rule[i].first.substr(2);
        if(collections.find(cat) == collections.end())
        {
          wcerr << L"Warning: Tag-rewrite rule '" << src << "' > '" << dest << "' contains mapping from undefined category '" << cat << "'." << endl;
          continue;
        }
        for(auto v : collections[cat]) vals[v].push_back(rule[i].second);
      }
      else
      {
        vals[rule[i].first].push_back(rule[i].second);
      }
    }
    if(src != dest)
    {
      for(auto a : collections[src])
      {
        if(vals.find(a) == vals.end())
        {
          bool found = false;
          for(auto b : collections[dest])
          {
            if(a == b)
            {
              found = true;
              break;
            }
          }
          if(!found)
          {
            wcerr << L"Warning: Tag-rewrite rule '" << src << "' > '" << dest << "' does not convert '" << a << "'." << endl;
          }
        }
        else if(vals[a].size() > 1)
        {
          wcerr << L"Warning: Tag-rewrite rule '" << src << "' > '" << dest << "' converts '" << a << "' to multiple values: ";
          for(auto b : vals[a]) wcerr << "'" << b << "', ";
          wcerr << "defaulting to '" << vals[a][0] << "'." << endl;
        }
      }
    }
  }
}

void
RTXCompiler::makePattern(int ruleid)
{
  Rule* rule = reductionRules[ruleid];
  if(rule->pattern.size() > longestPattern)
  {
    longestPattern = rule->pattern.size();
  }
  vector<vector<PatternElement*>> pat;
  for(unsigned int i = 0; i < rule->pattern.size(); i++)
  {
    vector<vector<wstring>> tags;
    tags.push_back(vector<wstring>());
    for(unsigned int j = 1; j < rule->pattern[i].size(); j++)
    {
      wstring tg = rule->pattern[i][j];
      if(rule->pattern[i][j][0] == L'[')
      {
        tg = tg.substr(1, tg.size()-2);
        vector<vector<wstring>> tmp;
        for(auto tls : tags)
        {
          for(auto t : collections[tg])
          {
            vector<wstring> tmp2;
            tmp2.assign(tls.begin(), tls.end());
            tmp2.push_back(t);
            tmp.push_back(tmp2);
          }
        }
        tags.swap(tmp);
      }
      else
      {
        for(unsigned int t = 0; t < tags.size(); t++)
        {
          tags[t].push_back(tg);
        }
      }
    }
    for(unsigned int t = 0; t < tags.size(); t++) tags[t].push_back(L"*");
    wstring lem = rule->pattern[i][0];
    if(lem.size() == 0 || lem[0] != L'$')
    {
      vector<PatternElement*> pel;
      for(auto tls : tags)
      {
        PatternElement* p = new PatternElement;
        p->lemma = lem;
        p->tags = tls;
        pel.push_back(p);
      }
      pat.push_back(pel);
    }
    else
    {
      vector<wstring> lems = collections[lem.substr(1)];
      vector<PatternElement*> el;
      for(unsigned int j = 0; j < lems.size(); j++)
      {
        for(auto tls : tags)
        {
          PatternElement* p = new PatternElement;
          p->lemma = lems[j];
          p->tags = tls;
          el.push_back(p);
        }
      }
      pat.push_back(el);
    }
  }
  PB.addPattern(pat, ruleid+1, rule->weight);
}

wstring
RTXCompiler::compileString(wstring s)
{
  wstring ret;
  ret += STRING;
  ret += (wchar_t)s.size();
  ret += s;
  return ret;
}

wstring
RTXCompiler::compileTag(wstring s)
{
  if(s.size() == 0)
  {
    return compileString(s);
  }
  wstring tag;
  tag += L'<';
  tag += s;
  tag += L'>';
  return compileString(tag);
}

wstring
RTXCompiler::compileClip(Clip* c, wstring _dest = L"")
{
  int src = (c->src == -1) ? 0 : c->src;
  bool useReplace = inOutputRule;
  wstring dest;
  if(inOutputRule && _dest.size() > 0)
  {
    dest = _dest;
  }
  else if(c->rewrite.size() > 0)
  {
    dest = c->rewrite;
  }
  wstring cl = (c->part == L"lemcase") ? compileString(L"lem") : compileString(c->part);
  cl += INT;
  cl += src;
  wstring ret = cl;
  wstring undeftag;
  wstring deftag;
  wstring thedefault;
  wstring blank;
  blank += DUP;
  blank += compileString(L"");
  blank += EQUAL;
  if(useReplace && undeftag.size() > 0)
  {
    blank += OVER;
    blank += compileTag(undeftag);
    blank += EQUAL;
    blank += OR;
  }
  if(attrDefaults.find(c->part) != attrDefaults.end())
  {
    undeftag = attrDefaults[c->part].first;
    deftag = attrDefaults[c->part].second;
    thedefault += DROP;
    thedefault += compileTag(useReplace ? deftag : undeftag);
    if(useReplace)
    {
      blank += OVER;
      blank += compileTag(undeftag);
      blank += EQUAL;
      blank += OR;
    }
  }
  blank += JUMPONFALSE;
  if(c->src == 0)
  {
    if(c->rewrite == L"lem" || c->rewrite == L"lemh" || c->rewrite == L"lemq")
    {
      return compileString(c->part);
    }
    else return compileTag(c->part);
  }
  else if(c->side == L"sl")
  {
    ret += SOURCECLIP;
    ret += blank;
    ret += (wchar_t)thedefault.size();
    ret += thedefault;
  }
  else if(c->side == L"ref")
  {
    ret += REFERENCECLIP;
    ret += blank;
    ret += (wchar_t)thedefault.size();
    ret += thedefault;
  }
  else if(c->side == L"tl" || c->part == L"lemcase" ||
          (c->src != -1 && !nodeIsSurface[currentRule->pattern[c->src-1][1]]))
  {
    ret += TARGETCLIP;
    ret += blank;
    ret += (wchar_t)thedefault.size();
    ret += thedefault;
  }
  else
  {
    ret += TARGETCLIP;
    ret += blank;
    ret += (wchar_t)(6 + 2*cl.size() + 2*blank.size() + thedefault.size());
    ret += DROP;
    ret += cl;
    ret += REFERENCECLIP;
    ret += blank;
    ret += (wchar_t)(3 + cl.size() + blank.size() + thedefault.size());
    ret += DROP;
    ret += cl;
    ret += SOURCECLIP;
    ret += blank;
    ret += (wchar_t)thedefault.size();
    ret += thedefault;
  }
  if(c->part == L"lemcase")
  {
    ret += GETCASE;
  }
  if(dest.size() > 0)
  {
    bool found = false;
    vector<pair<wstring, wstring>> rule;
    for(unsigned int i = 0; i < retagRules.size(); i++)
    {
      if(retagRules[i][0].first == c->part && retagRules[i][0].second == dest)
      {
        found = true;
        rule = retagRules[i];
        break;
      }
    }
    if(!found && dest != c->part)
    {
      die(L"There is no tag-rewrite rule from '" + c->part + L"' to '" + dest + L"'.");
    }
    wstring check;
    for(unsigned int i = 1; i < rule.size(); i++)
    {
      wstring cur;
      cur += DUP;
      if(rule[i].first.size() > 2 &&
         rule[i].first[0] == L'[' && rule[i].first[1] == L']')
      {
        cur += DISTAG;
        cur += compileString(rule[i].first.substr(2));
        cur += IN;
      }
      else
      {
        cur += compileTag(rule[i].first);
        cur += EQUAL;
      }
      cur += JUMPONFALSE;
      cur += (wchar_t)(rule[i].second.size() + (i == 1 ? 5 : 7));
      cur += DROP;
      cur += compileTag(rule[i].second);
      if(i != 1)
      {
        cur += JUMP;
        cur += (wchar_t)check.size();
      }
      check = cur + check;
    }
    ret += check;
  }
  return ret;
}

wstring
RTXCompiler::compileClip(wstring part, int pos, wstring side = L"")
{
  Clip cl;
  cl.part = part;
  cl.src = pos;
  cl.side = side;
  return compileClip(&cl);
}

RTXCompiler::Clip*
RTXCompiler::processMacroClip(Clip* mac, OutputChunk* arg)
{
  Clip* ret = new Clip;
  ret->part = mac->part;
  ret->side = mac->side;
  ret->rewrite = mac->rewrite;
  if(arg->pos == 0 && mac->src == 1)
  {
    if(arg->vars.find(mac->part) != arg->vars.end())
    {
      Clip* other = arg->vars[mac->part];
      ret->part = other->part;
      ret->side = other->side;
      ret->rewrite = other->rewrite; // TODO: what if they both have rewrite?
      ret->src = other->src;
    }
    else
    {
      die(L"Macro not given value for attribute '" + mac->part + L"'.");
    }
  }
  else
  {
    ret->src = (mac->src == 1) ? arg->pos : mac->src;
  }
  return ret;
}

RTXCompiler::Cond*
RTXCompiler::processMacroCond(Cond* mac, OutputChunk* arg)
{
  Cond* ret = new Cond;
  ret->op = mac->op;
  if(mac->op == 0)
  {
    ret->val = processMacroClip(mac->val, arg);
  }
  else
  {
    ret->left = processMacroCond(mac->left, arg);
    ret->right = processMacroCond(mac->right, arg);
  }
  return ret;
}

RTXCompiler::OutputChunk*
RTXCompiler::processMacroChunk(OutputChunk* mac, OutputChunk* arg)
{
  if(mac == NULL) return NULL;
  OutputChunk* ret = new OutputChunk;
  ret->mode = mac->mode;
  ret->lemma = mac->lemma;
  ret->tags = mac->tags;
  ret->getall = mac->getall;
  ret->pattern = mac->pattern;
  ret->conjoined = mac->conjoined;
  ret->nextConjoined = mac->nextConjoined;
  for(unsigned int i = 0; i < mac->children.size(); i++)
  {
    ret->children.push_back(processMacroChoice(mac->children[i], arg));
  }
  for(map<wstring, Clip*>::iterator it = mac->vars.begin(),
          limit = mac->vars.end(); it != limit; it++)
  {
    ret->vars[it->first] = processMacroClip(it->second, arg);
  }
  if(mac->pos == 1)
  {
    ret->pos = arg->pos;
    for(map<wstring, Clip*>::iterator it = arg->vars.begin(),
            limit = arg->vars.end(); it != limit; it++)
    {
      if(ret->vars.find(it->first) == ret->vars.end() || arg->pos == 0)
      {
        ret->vars[it->first] = it->second;
      }
    }
  }
  else
  {
    ret->pos = mac->pos;
  }
  return ret;
}

RTXCompiler::OutputChoice*
RTXCompiler::processMacroChoice(OutputChoice* mac, OutputChunk* arg)
{
  if(mac == NULL) return NULL;
  OutputChoice* ret = new OutputChoice;
  for(unsigned int i = 0; i < mac->conds.size(); i++)
  {
    ret->conds.push_back(processMacroCond(mac->conds[i], arg));
  }
  for(unsigned int i = 0; i < mac->nest.size(); i++)
  {
    ret->nest.push_back(processMacroChoice(mac->nest[i], arg));
  }
  for(unsigned int i = 0; i < mac->chunks.size(); i++)
  {
    ret->chunks.push_back(processMacroChunk(mac->chunks[i], arg));
  }
  return ret;
}

wstring
RTXCompiler::processOutputChunk(OutputChunk* r)
{
  wstring ret;
  if(r->mode == L"_")
  {
    ret += INT;
    ret += (wchar_t)r->pos;
    ret += BLANK;
    if(inOutputRule)
    {
      ret += OUTPUT;
    }
  }
  else if(r->mode == L"{}" || r->mode == L"[]" || r->mode == L"")
  {
    for(unsigned int i = 0; i < r->children.size(); i++)
    {
      ret += processOutputChoice(r->children[i]);
    }
  }
  else if(r->mode == L"#" || r->mode == L"#@")
  {
    wstring pos;
    if(r->pos != 0)
    {
      if(currentRule->pattern[r->pos-1].size() < 2)
      {
        die(L"could not find tag order for element " + to_wstring(r->pos));
      }
      wstring pos = currentRule->pattern[r->pos-1][1];
    }
    wstring patname = (r->pattern != L"") ? r->pattern : pos;
    pos = (pos != L"") ? pos : patname;
    if(outputRules.find(patname) == outputRules.end())
    {
      die(L"could not find output pattern '" + patname + L"'");
    }
    vector<wstring> pattern = outputRules[patname];

    if(r->getall)
    {
      for(unsigned int i = 0; i < parentTags.size(); i++)
      {
        if(r->vars.find(parentTags[i]) == r->vars.end())
        {
          Clip* cl = new Clip;
          cl->src = -1;
          cl->part = parentTags[i];
          r->vars[parentTags[i]] = cl;
        }
      }
    }

    if(pattern.size() == 1 && pattern[0] == L"macro")
    {
      if(r->nextConjoined)
      {
        die(L"Cannot currently conjoin to a macro.");
      }
      if(r->conjoined)
      {
        die(L"Cannot currently conjoin a macro.");
      }
      return processOutputChoice(processMacroChoice(macros[patname], r));
    }
    else if(r->conjoined)
    {
      ret += compileString(L"+");
      ret += APPENDSURFACE;
    }
    else
    {
      ret += CHUNK;
    }
    if(r->mode == L"#@")
    {
      unsigned int j;
      for(j = 0; j < r->lemma.size(); j++)
      {
        if(r->lemma[j] == L'#') break;
      }
      if(j < r->lemma.size())
      {
        ret += compileString(r->lemma.substr(0, j));
        Clip* c = new Clip;
        c->part = r->lemma.substr(j);
        c->src = 0;
        c->rewrite = L"lemq";
        r->vars[L"lemq"] = c;
      }
      else ret += compileString(r->lemma);
    }
    else if(r->vars.find(L"lem") != r->vars.end())
    {
      ret += compileClip(r->vars[L"lem"], L"lem");
    }
    else if(r->vars.find(L"lemh") != r->vars.end())
    {
      ret += compileClip(r->vars[L"lemh"], L"lemh");
    }
    else if(r->pos == 0)
    {
      ret += compileString(L"unknown");
    }
    else
    {
      Clip* c = new Clip;
      c->part = L"lemh";
      c->src = r->pos;
      c->side = L"tl";
      c->rewrite = L"lemh";
      ret += compileClip(c);
    }
    if(r->vars.find(L"lemcase") != r->vars.end())
    {
      ret += compileClip(r->vars[L"lemcase"], L"lemcase");
      ret += SETCASE;
    }
    ret += APPENDSURFACE;
    for(unsigned int i = 0; i < pattern.size(); i++)
    {
      if(pattern[i] == L"_")
      {
        if(r->pos != 0)
        {
          //ret += compileTag(currentRule->pattern[r->pos-1][1]);
          ret += compileClip(L"pos_tag", r->pos, L"tl");
        }
        else
        {
          ret += compileTag(pos);
        }
        ret += APPENDSURFACE;
      }
      else if(pattern[i][0] == L'<')
      {
        ret += compileString(pattern[i]);
        ret += APPENDSURFACE;
      }
      else
      {
        wstring ret_temp;
        vector<wstring> ops = altAttrs[pattern[i]];
        if(ops.size() == 0)
        {
          ops.push_back(pattern[i]);
        }
        wstring var;
        for(unsigned int v = 0; v < ops.size(); v++)
        {
          if(r->vars.find(ops[v]) != r->vars.end())
          {
            var = ops[v];
            break;
          }
        }
        if(var == L"" && r->pos != 0)
        {
          Clip* cl = new Clip;
          cl->src = r->pos;
          cl->part = pattern[i];
          ret_temp += compileClip(cl, pattern[i]);
        }
        else if(var == L"")
        {
          bool found = false;
          for(unsigned int t = 0; t < parentTags.size(); t++)
          {
            if(parentTags[t] == pattern[i])
            {
              ret_temp += compileTag(to_wstring(t+1));
              found = true;
              break;
            }
          }
          if(!found)
          {
            if(r->pos == 0 && currentRule->grab_all != -1)
            {
              ret_temp += compileClip(pattern[i], currentRule->grab_all);
            }
            else if(attrDefaults.find(pattern[i]) != attrDefaults.end())
            {
              ret_temp += compileTag(attrDefaults[pattern[i]].first);
            }
            else if(r->pos == 0)
            {
              die(L"Cannot find source for tag '" + pattern[i] + L"'.");
            }
            else
            {
              ret_temp += compileClip(pattern[i], r->pos);
            }
          }
        }
        else
        {
          ret_temp += compileClip(r->vars[var], pattern[i]);
        }
        if(inOutputRule && noOverwrite[pattern[i]].size() > 0)
        {
          ret += compileClip(pattern[i], r->pos, L"tl");
          ret += DUP;
          ret += compileString(pattern[i] + L" over");
          ret += IN;
          ret += JUMPONTRUE;
          ret += (wchar_t)(1+ret_temp.size());
          ret += DROP;
        }
        ret += ret_temp;
        ret += APPENDSURFACE;
      }
    }
    if(r->vars.find(L"lemq") != r->vars.end())
    {
      ret += compileClip(r->vars[L"lemq"]);
      ret += APPENDSURFACE;
    }
    else if(r->pos != 0)
    {
      ret += compileClip(L"lemq", r->pos, L"tl");
      ret += APPENDSURFACE;
    }
    if(r->pos != 0 && inOutputRule)
    {
      ret += compileClip(L"whole", r->pos, L"tl");
      ret += APPENDALLCHILDREN;
      ret += INT;
      ret += (wchar_t)r->pos;
      ret += GETRULE;
      ret += INT;
      ret += (wchar_t)0;
      ret += SETRULE;
    }
    if(inOutputRule && !r->nextConjoined)
    {
      ret += OUTPUT;
    }
  }
  else
  {
    ret += CHUNK;
    ret += compileString(r->lemma);
    ret += APPENDSURFACE;
    for(unsigned int i = 0; i < r->tags.size(); i++)
    {
      ret += compileClip(r->vars[r->tags[i]]);
      ret += APPENDSURFACE;
    }
    if(inOutputRule && !r->nextConjoined)
    {
      ret += OUTPUT;
    }
  }
  return ret;
}

wstring
RTXCompiler::processCond(Cond* cond)
{
  wstring ret;
  if(cond == NULL)
  {
    ret += PUSHTRUE;
    return ret;
  }
  if(cond->op == AND)
  {
    if(cond->left->op == 0 || cond->right->op == 0)
    {
      die(L"Cannot evaluate AND with string as operand (try adding parentheses).");
    }
  }
  else if(cond->op == OR)
  {
    if(cond->left->op == 0 || cond->right->op == 0)
    {
      die(L"Cannot evaluate OR with string as operand (try adding parentheses).");
    }
  }
  else if(cond->op == NOT)
  {
    if(cond->right->op == 0)
    {
      die(L"Attempt to negate string value.");
    }
  }
  else if(cond->op != 0 && (cond->left->op != 0 || cond->right->op != 0))
  {
    die(L"String operator cannot take condition as operand.");
  }
  else if(cond->op == EQUAL)
  {
    wstring lit;
    wstring attr;
    wstring rew;
    Clip* l = cond->left->val;
    if(l->src == 0) lit = l->part;
    else
    {
      attr = l->part;
      if(l->rewrite.size() > 0) rew = l->rewrite;
    }
    Clip* r = cond->right->val;
    if(r->src == 0) lit = r->part;
    else
    {
      attr = r->part;
      if(r->rewrite.size() > 0) rew = r->rewrite;
    }
    if(lit.size() > 0 && attr.size() > 0 && rew.size() == 0
        && collections.find(attr) != collections.end())
    {
      bool found = false;
      for(auto tag : collections[attr])
      {
        if(tag == lit)
        {
          found = true;
          break;
        }
      }
      if(!found) die(L"'" + lit + L"' is not an element of list '" + attr + L"', so this check will always fail.");
    }
  }
  if(cond->op == 0)
  {
    if(cond->val->src == 0)
    {
      ret = compileString(cond->val->part);
    }
    else
    {
      ret = compileClip(cond->val);
      if(cond->val->part != L"lem" && cond->val->part != L"lemh" && cond->val->part != L"lemq")
      {
        ret += DISTAG;
      }
    }
  }
  else if(cond->op == NOT)
  {
    ret = processCond(cond->right);
    ret += NOT;
  }
  else
  {
    ret = processCond(cond->left);
    ret += processCond(cond->right);
    ret += cond->op;
  }
  return ret;
}

wstring
RTXCompiler::processOutputChoice(OutputChoice* choice)
{
  wstring ret;
  if(choice->nest.back() == NULL)
  {
    ret += processOutputChunk(choice->chunks.back());
  }
  else
  {
    ret += processOutputChoice(choice->nest.back());
  }
  int n = choice->conds.size();
  for(int i = 1; i <= n; i++)
  {
    wstring act;
    if(choice->nest[n-i] != NULL)
    {
      act = processOutputChoice(choice->nest[n-i]);
    }
    else
    {
      act = processOutputChunk(choice->chunks[n-i]);
    }
    act += JUMP;
    act += (wchar_t)ret.size();
    wstring cond = processCond(choice->conds[n-i]);
    cond += JUMPONFALSE;
    cond += (wchar_t)act.size();
    ret = cond + act + ret;
  }
  return ret;
}

void
RTXCompiler::processRules()
{
  Rule* rule;
  for(unsigned int ruleid = 0; ruleid < reductionRules.size(); ruleid++)
  {
    rule = reductionRules[ruleid];
    if(summarizing)
    {
      for(auto it : rule->result) wcerr << it << " ";
      wcerr << "->";
      for(auto it : rule->pattern) wcerr << " " << it[1];
      wcerr << endl;
    }
    currentRule = rule;
    currentChunk = NULL;
    currentChoice = NULL;
    makePattern(ruleid);
    wstring comp;
    if(rule->cond != NULL)
    {
      comp = processCond(rule->cond) + JUMPONTRUE + (wchar_t)1 + REJECTRULE;
    }
    vector<wstring> outcomp;
    outcomp.resize(rule->pattern.size());
    parentTags.clear();
    int patidx = 0;
    for(unsigned int i = 0; i < rule->output.size(); i++)
    {
      OutputChoice* cur = rule->output[i];
      if(cur->chunks.size() == 1 && cur->chunks[0]->mode == L"_")
      {
        comp += processOutputChoice(cur);
      }
      else if(cur->chunks.size() == 1 && cur->chunks[0]->mode == L"#")
      {
        comp += processOutputChoice(cur);
        patidx++;
      }
      else
      {
        OutputChunk* ch = new OutputChunk;
        ch->mode = L"#";
        ch->pos = 0;
        ch->getall = true;
        ch->vars = rule->vars;
        ch->conjoined = false;
        ch->nextConjoined = false;
        ch->pattern = rule->result[patidx];
        comp += processOutputChunk(ch);
        comp += INT;
        comp += (wchar_t)outputBytecode.size();
        comp += INT;
        comp += (wchar_t)0;
        comp += SETRULE;
        comp += APPENDALLINPUT;
        parentTags = outputRules[ch->pattern];
        inOutputRule = true;
        outputBytecode.push_back(processOutputChoice(cur));
        PB.outRuleNames.push_back(L"line " + to_wstring(rule->line));
        inOutputRule = false;
        parentTags.clear();
        patidx++;
      }
      comp += OUTPUT;
    }
    rule->compiled = comp;
  }
}

void
RTXCompiler::buildLookahead()
{
  vector<pair<wstring, vector<wstring>>> rules;
  map<wstring, set<wstring>> first;
  for(unsigned int i = 0; i < reductionRules.size(); i++)
  {
    vector<wstring> parts;
    for(unsigned int j = 0; j < reductionRules[i]->pattern.size(); j++)
    {
      parts.push_back(reductionRules[i]->pattern[j][1]);
    }
    rules.push_back(make_pair(reductionRules[i]->result[0], parts));
    first[reductionRules[i]->result[0]].insert(parts[0]);
  }
  for(unsigned int i = 0; i < rules.size(); i++)
  {
    vector<PatternElement*> check;
    set<wstring> ops;
    for(unsigned int j = 0; j < rules.size(); j++)
    {
      if(rules[j].second.size() <= rules[i].second.size()) continue;
      bool match = true;
      for(unsigned int k = 0; k < rules[i].second.size(); k++)
      {
        if(rules[i].second[k] != rules[j].second[k])
        {
          match = false;
          break;
        }
      }
      if(!match) continue;
      wstring next = rules[j].second[rules[i].second.size()];
      vector<wstring> todo;
      todo.push_back(next);
      while(todo.size() > 0)
      {
        wstring cur = todo.back();
        todo.pop_back();
        if(ops.count(cur) == 0)
        {
          ops.insert(cur);
          PatternElement* p = new PatternElement;
          p->tags.push_back(cur);
          p->tags.push_back(L"*");
          check.push_back(p);
          if(first.find(cur) != first.end())
          {
            todo.insert(todo.end(), first[cur].begin(), first[cur].end());
          }
        }
      }
    }
    PB.addLookahead(i, check);
  }
}

void
RTXCompiler::read(const string &fname)
{
  currentLine = 1;
  sourceFile = fname;
  source.open(fname);
  while(true)
  {
    eatSpaces();
    if(source.eof())
    {
      break;
    }
    parseRule();
  }
  source.close();
  errorsAreSyntax = false;
  processRetagRules();
  processRules();
  for(map<wstring, vector<wstring>>::iterator it=collections.begin(); it != collections.end(); ++it)
  {
    set<wstring, Ltstr> vals;
    for(unsigned int i = 0; i < it->second.size(); i++)
    {
      vals.insert(it->second[i]);
    }
    PB.addList(it->first, vals);
    PB.addAttr(it->first, vals);
  }
  buildLookahead();
}

void
RTXCompiler::write(const string &fname)
{
  FILE *out = fopen(fname.c_str(), "wb");
  if(!out)
  {
    cerr << "Error: cannot open '" << fname;
    cerr << "' for writing" << endl;
    exit(EXIT_FAILURE);
  }

  vector<PatternElement*> glue;
  set<wstring, Ltstr> seen;
  vector<pair<int, wstring>> inRules;
  for(unsigned int i = 0; i < reductionRules.size(); i++)
  {
    inRules.push_back(make_pair(2*reductionRules[i]->pattern.size() - 1,
                                reductionRules[i]->compiled));
    PB.inRuleNames.push_back(L"line " + to_wstring(reductionRules[i]->line));
    wstring tg = reductionRules[i]->result.back();
    if(seen.find(tg) == seen.end())
    {
      PatternElement* p = new PatternElement;
      p->tags.push_back(tg);
      p->tags.push_back(L"*");
      glue.push_back(p);
      seen.insert(tg);
    }
  }
  PatternElement* p = new PatternElement;
  p->tags.push_back(L"FALL:BACK");
  vector<vector<PatternElement*>> fb;
  fb.push_back(glue);
  fb.push_back(vector<PatternElement*>(1, p));
  if(fallbackRule)
  {
    PB.addPattern(fb, -1);
  }

  PB.write(out, longestPattern, inRules, outputBytecode);

  fclose(out);
}
