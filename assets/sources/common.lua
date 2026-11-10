local M = {}
function M.absolute(home, url)
  url = (url or ''):gsub('#.*$', '')
  if url:match('^https://') then return url end
  if url:sub(1,2)=='//' then return 'https:'..url end
  if url:sub(1,1)=='/' then return home..url end
  return home..'/'..url
end
function M.encode(s) return (s:gsub('[^%w%-_%.~]',function(c) return string.format('%%%02X',c:byte()) end)) end
function M.first(html, tag, attr, val)
  return select_html(html, tag or '', attr or '', val or '')[1]
end
function M.text(html, tag, attr, val)
  local n=M.first(html,tag,attr,val); return n and n.text or ''
end
function M.cover(html, home)
  local meta=M.first(html,'meta','property','og:image') or M.first(html,'meta','name','twitter:image')
  local url=meta and meta.attrs.content or ''
  if url=='' then
    local img=M.first(html,'img','class','wp-post-image') or M.first(html,'img','itemprop','image')
    url=img and (img.attrs['data-src'] or img.attrs.src) or ''
  end
  if url=='' then return '' end
  url=url:gsub('^http://','https://')
  return M.absolute(home,url)
end
function M.links(html, home, pattern, chapters)
  local result, seen={},{}
  for _,n in ipairs(select_html(html,'a','','')) do
    local url=M.absolute(home,n.attrs.href)
    if url:match(pattern) and not seen[url] then
      seen[url]=true
      local title = n.attrs.title or n.text
      if not n.attrs.title then
        local heading=M.text(n.html,'h3')
        if heading=='' then heading=M.text(n.html,'h2') end
        if chapters then
          local chapter_title=M.text(n.html,'p','class','chapter-card__title')
          if chapter_title~='' then heading=chapter_title end
        end
        if heading~='' then title=heading end
      end
      title=title:gsub('%s+',' ')
      if title=='' then title=url:match('([^/]+)$') or url end
      local locked=n.attrs['data-premium']=='1' or n.html:find('Locked chapter',1,true) or n.html:find('lucide%-lock') or n.html:find('fa%-lock') or n.html:find('Premium') or n.html:find('premium%-badge')
      result[#result+1]={url=url,title=title,locked=chapters and not not locked or false}
    end
  end
  return result
end
function M.next(html, home)
  local n=M.first(html,'a','rel','next'); return n and M.absolute(home,n.attrs.href) or ''
end
function M.sort_chapters(items)
  local function number(x) return tonumber(x.url:match('/([%d%.]+)$') or x.title:match('[Cc]hapter%s*([%d%.]+)') or x.title:match('([%d%.]+)')) or 0 end
  table.sort(items,function(a,b) local x,y=number(a),number(b); if x==y then return a.url<b.url end; return x<y end)
  return items
end
return M
