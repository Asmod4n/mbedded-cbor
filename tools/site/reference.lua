local tree, group, impl, commit, examples_path

local function read_file(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local text = f:read("a")
  f:close()
  return text
end

local function html_escaped(text)
  return (text:gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;"):gsub('"', "&quot;"))
end

local function json_string(text)
  return '"' .. text:gsub('[%c"\\]', function(c)
    if c == '"' then return '\\"' end
    if c == "\\" then return "\\\\" end
    return string.format("\\u%04x", c:byte())
  end) .. '"'
end

local function page_id(file)
  return "m-" .. file:gsub("%.md$", "")
end

local function link_target(target)
  if target:match("^%a+:") or target:match("^#") then return target end
  local anchor = target:match("(#.*)$") or ""
  local path = target:gsub("#.*$", "")
  local g, file = path:match("^%.%./(%w+)/([%w_]+)%.md$")
  if g == nil then file = path:match("^([%w_]+)%.md$"); g = group end
  if g and file == "index" then return g .. ".html" end
  if g and file then return (g == group and "" or g .. ".html") .. "#" .. page_id(file) end
  g = path:match("^(%w+)/index%.md$")
  if g then return g .. ".html" end
  local guide = path:match("^%.%./%.%./([%w_/]+%.md)$") or path:match("^%.%./([%w_/]+%.md)$")
  if guide then
    return "https://github.com/Asmod4n/mbedded-cbor/blob/" .. commit .. "/doc/" .. guide .. anchor
  end
  if path == ".." or path == "../" then
    return "https://github.com/Asmod4n/mbedded-cbor/tree/" .. commit .. "/doc"
  end
  error("reference.lua: a link with no target on the site: " .. target)
end

local links = { Link = function(l) l.target = link_target(l.target); return l end }

local function callseq_html(text)
  local lines = {}
  for line in text:gmatch("[^\n]+") do
    local call, result = line:match("^(.-)%s*→%s*(.*)$")
    if call then
      lines[#lines + 1] = html_escaped(call) .. ' <span class="arrow">→</span> ' .. html_escaped(result)
    else
      lines[#lines + 1] = html_escaped(line)
    end
  end
  return '<pre class="callseq">' .. table.concat(lines, "\n") .. "</pre>"
end

local function example_read(text, where)
  local spec = { calls = {} }
  for line in text:gmatch("[^\n]+") do
    local key, value = line:match("^(%w+):%s?(.*)$")
    if key == "message" or key == "bytes" then
      spec[key] = value
    elseif key == "call" then
      spec.calls[#spec.calls + 1] = { call = value }
    elseif key == "result" and #spec.calls > 0 and spec.calls[#spec.calls].result == nil then
      spec.calls[#spec.calls].result = value
    else
      error("reference.lua: " .. where .. ": the example line is not message, bytes, call or result: " .. line)
    end
  end
  if (spec.message == nil) == (spec.bytes == nil) then
    error("reference.lua: " .. where .. ": an example has one message or one bytes line")
  end
  for _, c in ipairs(spec.calls) do
    if c.result == nil then error("reference.lua: " .. where .. ": the call has no result: " .. c.call) end
  end
  if #spec.calls == 0 then error("reference.lua: " .. where .. ": the example has no call") end
  return spec
end

local function example_html(spec, n)
  local calls, shown = {}, {}
  for _, c in ipairs(spec.calls) do
    calls[#calls + 1] = c.call
    shown[#shown + 1] = '<span class="call">' .. html_escaped(c.call) .. '</span>\n'
      .. '<span class="' .. ((c.result:match("^fault") or c.result:match("^unexpected")) and "err" or "ok") .. '">'
      .. html_escaped(c.result) .. "</span>"
  end
  local id = "x" .. n
  local message
  if spec.message then
    message = '<div class="field"><label for="' .. id .. '-m">Message</label><textarea id="' .. id
      .. '-m" data-message spellcheck="false">' .. html_escaped(spec.message) .. "</textarea></div>\n"
  else
    message = '<div class="field"><label for="' .. id .. '-m">Message</label><textarea id="' .. id
      .. '-m" data-notation readonly spellcheck="false"></textarea></div>\n'
  end
  return '<div class="play">\n<div class="bar"><span class="label">Example</span>'
    .. '<button class="run" type="button" hidden>▶ Run</button></div>\n<div class="fields">\n' .. message
    .. '<div class="field"><label for="' .. id .. '-b">Bytes</label><input id="' .. id
    .. '-b" class="hex" data-bytes spellcheck="false" value="' .. html_escaped(spec.bytes or "") .. '"></div>\n'
    .. '<div class="field"><label for="' .. id .. '-c">Calls</label><textarea id="' .. id
    .. '-c" data-calls spellcheck="false" wrap="off" rows="' .. #calls .. '">' .. html_escaped(table.concat(calls, "\n"))
    .. "</textarea></div>\n</div>\n"
    .. '<div class="out" data-out aria-live="polite">' .. table.concat(shown, "\n") .. "</div>\n</div>"
end

local function implementation_html(file)
  local text = read_file(impl .. "/" .. group .. "/" .. file:gsub("%.md$", "") .. ".html")
  if text == nil then return nil end
  return '<details class="src"><summary>Show the implementation</summary>\n' .. text
    .. '<p class="note">Each line number opens that line on GitHub, at the commit the page was built from.</p>\n'
    .. "</details>"
end

local function types_html(path, namespace)
  local text = read_file(tree .. "/" .. path)
  if text == nil then error("reference.lua: cannot read " .. path) end
  local open = "namespace " .. namespace .. "\n{\n"
  local close = "} // namespace " .. namespace .. "\n"
  local first = text:find(open, 1, true)
  local _, last = text:find(close, first or 1, true)
  if first == nil or last == nil then error("reference.lua: " .. path .. " has no namespace " .. namespace) end
  local parts = { text:sub(first, last) }
  for chunk in (text:sub(last + 1) .. "\n\n"):gmatch("(.-)\n\n+") do
    if chunk:find(namespace .. "::", 1, true) and not chunk:find("^namespace ") then
      parts[#parts + 1] = chunk:gsub("^\n+", "") .. "\n"
    end
  end
  return '<section class="method" id="types"><h2>Types of the examples</h2>\n'
    .. '<p>The WebAssembly module of this page holds these types, from <a href="https://github.com/Asmod4n/mbedded-cbor/blob/'
    .. commit .. "/" .. path .. '">' .. path .. "</a>.</p>\n"
    .. '<pre class="types"><code>' .. html_escaped(table.concat(parts, "\n")) .. "</code></pre>\n</section>"
end

local examples = {}

local function page_blocks(file, title, does)
  local where = group .. "/" .. file
  local text = read_file(tree .. "/doc/reference/" .. group .. "/" .. file)
  if text == nil then error("reference.lua: cannot read " .. where) end
  local doc = pandoc.read(text, "gfm"):walk(links)
  local out = { pandoc.RawBlock("html", '<section class="method" id="' .. page_id(file) .. '">'),
    pandoc.RawBlock("html", "<h2>" .. html_escaped(title) .. "</h2>") }
  local callseq, example, skipping = false, false, false
  local plays = {}
  local function plays_put()
    for _, p in ipairs(plays) do out[#out + 1] = p end
    plays = {}
  end
  for _, b in ipairs(doc.blocks) do
    if b.t == "Header" and b.level == 1 then
    elseif b.t == "Header" and b.level == 2 then
      plays_put()
      local name = pandoc.utils.stringify(b)
      skipping = name == "Example" or name == "Examples"
      if not skipping then
        b.level = 3
        out[#out + 1] = b
      end
    elseif b.t == "CodeBlock" and b.classes[1] == "call-seq" then
      callseq = true
      out[#out + 1] = pandoc.RawBlock("html", callseq_html(b.text))
    elseif b.t == "CodeBlock" and b.classes[1] == "example" then
      example = true
      local spec = example_read(b.text, where)
      spec.page = where
      examples[#examples + 1] = spec
      plays[#plays + 1] = pandoc.RawBlock("html", example_html(spec, #examples))
    elseif b.t == "CodeBlock" then
    elseif not skipping then
      out[#out + 1] = b
    end
  end
  plays_put()
  if not callseq then error("reference.lua: " .. where .. " has no call-seq block") end
  if not example then error("reference.lua: " .. where .. " has no example block") end
  local source = implementation_html(file)
  if source then out[#out + 1] = pandoc.RawBlock("html", source) end
  out[#out + 1] = pandoc.RawBlock("html", "</section>")
  return out, pandoc.utils.stringify(does)
end

local function examples_write()
  local rows = {}
  for _, s in ipairs(examples) do
    local calls = {}
    for _, c in ipairs(s.calls) do
      calls[#calls + 1] = '{"call": ' .. json_string(c.call) .. ', "result": ' .. json_string(c.result) .. "}"
    end
    rows[#rows + 1] = '{"page": ' .. json_string(s.page) .. ', '
      .. (s.message and '"message": ' .. json_string(s.message) or '"bytes": ' .. json_string(s.bytes))
      .. ', "calls": [' .. table.concat(calls, ", ") .. "]}"
  end
  local f = assert(io.open(examples_path, "wb"))
  f:write("[\n" .. table.concat(rows, ",\n") .. "\n]\n")
  f:close()
end

function Pandoc(doc)
  tree = pandoc.utils.stringify(doc.meta.tree)
  group = pandoc.utils.stringify(doc.meta.group)
  impl = pandoc.utils.stringify(doc.meta.impl)
  commit = pandoc.utils.stringify(doc.meta.commit)
  examples_path = pandoc.utils.stringify(doc.meta.examples)
  local groups = {}
  for _, b in ipairs(pandoc.read(read_file(tree .. "/doc/reference/index.md"), "gfm").blocks) do
    if b.t == "Table" then
      for _, row in ipairs(b.bodies[1].body) do
        local name = pandoc.utils.stringify(row.cells[1].contents)
        groups[#groups + 1] = pandoc.MetaMap({ name = pandoc.MetaString(name),
          href = pandoc.MetaString(name .. ".html"),
          here = pandoc.MetaBool(name == group) })
      end
    end
  end
  doc.meta.groups = pandoc.MetaList(groups)
  if group == "index" then
    doc.meta.pagetitle = "mbedded-cbor"
    return doc:walk(links)
  end
  local blocks, methods = {}, {}
  for _, b in ipairs(doc.blocks) do
    if b.t == "Header" and b.level == 1 then
      doc.meta.pagetitle = pandoc.utils.stringify(b)
    elseif b.t == "Para" and #b.content >= 1 and pandoc.utils.stringify(b):match("^The examples use the types in") then
      local link, namespace
      for _, i in ipairs(b.content) do
        if i.t == "Link" then link = i.target end
        if i.t == "Code" then namespace = i.text end
      end
      local path = link:gsub("^[./]*", "")
      table.insert(blocks, 1, pandoc.RawBlock("html", types_html(path, namespace)))
      table.insert(methods, 1, pandoc.MetaMap({ name = pandoc.MetaString("types"),
        id = pandoc.MetaString("types"), does = pandoc.MetaString("the types of the examples") }))
    elseif b.t == "Para" and doc.meta.lead == nil then
      doc.meta.lead = pandoc.MetaInlines(b:walk(links).content)
    elseif b.t == "Table" then
      for _, row in ipairs(b.bodies[1].body) do
        local first = row.cells[1].contents[1]
        local link = first and pandoc.utils.type(first) == "Block" and first.content[1]
        if link and link.t == "Link" then
          local file = link.target
          local title = pandoc.utils.stringify(link.content)
          local page, does = page_blocks(file, title, row.cells[2].contents)
          for _, p in ipairs(page) do blocks[#blocks + 1] = p end
          methods[#methods + 1] = pandoc.MetaMap({ name = pandoc.MetaString(title),
            id = pandoc.MetaString(page_id(file)), does = pandoc.MetaString(does) })
        end
      end
    end
  end
  doc.meta.methods = pandoc.MetaList(methods)
  examples_write()
  return pandoc.Pandoc(blocks, doc.meta)
end
