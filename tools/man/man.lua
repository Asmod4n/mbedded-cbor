local tree, out, version, date = arg[1], arg[2], arg[3], arg[4]
local reference = tree .. '/doc/reference/'

local function text_of(path)
  local f, err = io.open(path)
  if not f then error(err) end
  local text = f:read('a')
  f:close()
  return text
end

local function written(path, text)
  local f, err = io.open(path, 'w')
  if not f then error(err) end
  f:write(text)
  f:close()
end

local function unlinked(blocks)
  return pandoc.Blocks(blocks):walk({Link = function(l) return l.content end})
end

local function first_sentence(blocks)
  local s = pandoc.utils.stringify(blocks[1])
  return (s:match('^(.-)%.%s') or s:match('^(.-)%.$') or s)
end

local function rows(text)
  local found = {}
  for line in text:gmatch('[^\n]+') do
    local label, file, rest = line:match('^| %[(.-)%]%(([%w_/.]+)%) | (.*) |$')
    if label then
      found[#found + 1] = {label = label, file = file, rest = rest}
    end
  end
  return found
end

local function names_of(title)
  local found, prefix = {}, nil
  for name in (title .. ', '):gmatch('(.-), ') do
    if prefix == nil then
      prefix = name:match('^(.*)::[^:]*$') or ''
    elseif prefix ~= '' then
      name = prefix .. '::' .. name
    end
    if not name:find(' ') then found[#found + 1] = name end
  end
  return found
end

local placed = {
  ['Parameters'] = 'DESCRIPTION', ['What is checked'] = 'DESCRIPTION',
  ['What is not checked'] = 'DESCRIPTION', ['Return value'] = 'RETURN VALUE',
  ['Errors'] = 'ERRORS', ['Cost'] = 'NOTES', ['Example'] = 'EXAMPLES', ['Examples'] = 'EXAMPLES',
  ['See also'] = false,
}

local order = {'SYNOPSIS', 'DESCRIPTION', 'RETURN VALUE', 'ERRORS', 'NOTES', 'EXAMPLES'}

local function page_parts(path)
  local text = text_of(path)
  local doc = pandoc.read(text, 'gfm')
  local p = {title = text:match('^# ([^\n]+)'), sections = {}}
  if not p.title then error(path .. ': the page starts with no title') end
  for _, s in ipairs(order) do p.sections[s] = pandoc.Blocks{} end
  local current, leading = 'DESCRIPTION', true
  for _, b in ipairs(doc.blocks) do
    if b.t == 'Header' and b.level == 1 then
    elseif b.t == 'Header' and b.level == 2 then
      local name = pandoc.utils.stringify(b)
      local section = placed[name]
      if section == nil then error(path .. ': no man section for the heading "' .. name .. '"') end
      current = section
      leading = false
      if current == 'DESCRIPTION' then p.sections[current]:insert(pandoc.Para{pandoc.Strong(name)}) end
    elseif b.t == 'CodeBlock' and (b.classes[1] == 'call-seq' or b.classes[1] == 'example') then
    elseif leading and b.t == 'CodeBlock' and b.classes[1] == 'cpp' then
      p.sections['SYNOPSIS']:insert(b)
    elseif current then
      leading = false
      p.sections[current]:insert(b)
    end
  end
  for s, blocks in pairs(p.sections) do p.sections[s] = unlinked(blocks) end
  return p
end

local function man_page(name, lead, intro, paths)
  local parts = {}
  for _, path in ipairs(paths) do parts[#parts + 1] = page_parts(path) end
  local names = {}
  for _, p in ipairs(parts) do
    for _, n in ipairs(names_of(p.title)) do names[#names + 1] = n end
  end
  if #names == 0 or names[1] ~= name then table.insert(names, 1, name) end
  local blocks = pandoc.Blocks{pandoc.Header(1, 'NAME'), pandoc.Para{pandoc.Str(table.concat(names, ', ') .. ' - ' .. lead)}}
  for _, s in ipairs(order) do
    local body = pandoc.Blocks{}
    if s == 'SYNOPSIS' then body:insert(pandoc.CodeBlock('#include <cbor/cbor.hpp>', {class = 'cpp'})) end
    if s == 'DESCRIPTION' then body:extend(intro) end
    for _, p in ipairs(parts) do
      local own = p.sections[s]
      if #own > 0 then
        if s ~= 'SYNOPSIS' and #parts > 1 then body:insert(pandoc.Header(2, pandoc.Str(p.title))) end
        body:extend(own)
      end
    end
    if #body > 0 then
      blocks:insert(pandoc.Header(1, s))
      blocks:extend(body)
    end
  end
  return blocks
end

local pages = {}

local index = pandoc.read(text_of(reference .. 'index.md'), 'gfm')
local goal, choice, in_choice = pandoc.Blocks{}, pandoc.Blocks{}, false
for i, b in ipairs(index.blocks) do
  if b.t == 'Header' and b.level == 2 then
    in_choice = pandoc.utils.stringify(b) == 'Which class to use'
  elseif in_choice then
    choice:insert(b.t == 'Header' and pandoc.Header(2, b.content) or b)
  elseif b.t == 'Para' and #goal == 0 then
    goal:insert(b)
  end
end
local library_lead = first_sentence(goal):gsub('^mbedded%-cbor is ', '')

for _, row in ipairs(rows(text_of(reference .. 'index.md'))) do
  local group = row.label
  local group_text = text_of(reference .. group .. '/index.md')
  local group_doc = pandoc.read(group_text, 'gfm')
  local intro = pandoc.Blocks{}
  for _, b in ipairs(group_doc.blocks) do
    if b.t == 'Para' then intro:insert(b) elseif b.t == 'Table' then break end
  end
  intro = unlinked(intro)
  local entries = rows(group_text)
  if group == 'tools' then
    for _, e in ipairs(entries) do
      pages[#pages + 1] = {file = e.label, name = 'cbor::' .. e.label, lead = e.rest,
        intro = pandoc.Blocks{}, paths = {reference .. group .. '/' .. e.file}}
    end
  else
    local first = row.rest:match('`([^`]+)`')
    local name = first:gsub('<.*>', '')
    local lead = row.rest:match(': (.*)$') or first_sentence(intro):gsub('^%u', string.lower)
    local paths = {}
    for _, e in ipairs(entries) do paths[#paths + 1] = reference .. group .. '/' .. e.file end
    pages[#pages + 1] = {file = name:gsub('^cbor::', ''), name = name, lead = lead, intro = intro, paths = paths}
  end
end

local function see_also(except)
  local names = {}
  if except ~= 'mbedded-cbor' then names[#names + 1] = 'mbedded-cbor(3)' end
  for _, p in ipairs(pages) do
    if p.name ~= except then names[#names + 1] = p.name .. '(3)' end
  end
  return pandoc.Blocks{pandoc.Header(1, 'SEE ALSO'), pandoc.Para{pandoc.Str(table.concat(names, ', '))}}
end

local template = pandoc.template.compile(pandoc.template.default('man'))

local function page_written(file, title, blocks)
  local meta = {title = title, section = '3', date = pandoc.Inlines{pandoc.RawInline('man', date)}, footer = 'mbedded-cbor ' .. version, header = 'mbedded-cbor'}
  local text = pandoc.write(pandoc.Pandoc(blocks, meta), 'man', {template = template})
  written(out .. '/' .. file .. '.3', (text:gsub('(\n%.SS [^\n]*\n)%.PP\n', '%1')))
end

local library = pandoc.Blocks{pandoc.Header(1, 'NAME'), pandoc.Para{pandoc.Str('mbedded-cbor - ' .. library_lead)},
  pandoc.Header(1, 'DESCRIPTION')}
library:extend(unlinked(goal))
library:extend(unlinked(choice))
library:extend(see_also('mbedded-cbor'))
page_written('mbedded-cbor', 'mbedded-cbor', library)

for _, p in ipairs(pages) do
  local blocks = man_page(p.name, p.lead, p.intro, p.paths)
  blocks:extend(see_also(p.name))
  page_written(p.file, p.name, blocks)
end
