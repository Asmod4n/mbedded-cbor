-- Keeps the text of each link of a guide and drops its target, because a man page has no link. Sets the date as
-- man text, so that mandoc reads it as a date.
function Link(l)
  return l.content
end

function Meta(m)
  m.date = pandoc.Inlines{pandoc.RawInline('man', os.getenv('TODAY'))}
  return m
end
