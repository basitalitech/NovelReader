local H,home=common,'https://asurascans.com'
return function(op,url,query)
  if op=='browse' then
    if query~='' then
      local data=json(get('https://api.asurascans.com/api/novel-series?search='..H.encode(query)..'&limit=30'))
      local items={}
      for _,n in ipairs(data.data or {}) do items[#items+1]={title=n.title,url=home..'/novels/'..n.slug} end
      return {items=items}
    end
    local html=get(url~='' and url or home..'/novels')
    local items=H.links(html,home,'^https://asurascans%.com/novels/[^/%?]+$',false)
    local filtered={}
    for _,item in ipairs(items) do
      if item.url~=home..'/novels/browse' and (query=='' or item.title:lower():find(query:lower(),1,true)) then filtered[#filtered+1]=item end
    end
    return {items=filtered}
  end
  local html=get(url)
  if op=='chapters' then
    local title=H.text(html,'h1')
    local list
    for _,n in ipairs(select_html(html,'astro-island','component-url','')) do
      if n.attrs['component-url']:find('NovelChapterList',1,true) then list=n.html; break end
    end
    assert(list,'Chapter list unavailable; source layout may have changed.')
    local items=H.links(list,home,'^https://asurascans%.com/novels/[^/]+/chapter/[%d%.]+$',true)
    local description=H.first(html,'meta','property','og:description')
    return {title=title,cover=H.cover(html,home),description=description and description.attrs.content or '',items=H.sort_chapters(items)}
  end
  local paragraphs={}
  for _,p in ipairs(select_html(html,'p','data-para','')) do paragraphs[#paragraphs+1]=p.text end
  assert(#paragraphs>0,'Chapter unavailable: locked, login required, or source layout changed.')
  return {text=table.concat(paragraphs,'\n\n')}
end
