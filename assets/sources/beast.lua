local H,home=common,'https://beastnovels.com'
return function(op,url,query)
  if op=='browse' then
    if query~='' then
      local data=json(get(home..'/search/books?q='..H.encode(query)))
      local items={}
      for _,n in ipairs(data) do items[#items+1]={title=n.name,url=n.url} end
      return {items=items}
    end
    local html=get(url~='' and url or home)
    return {items=H.links(html,home,'^https://beastnovels%.com/series/[^/%?]+$',false),next=H.next(html,home)}
  end
  local html=get(url)
  if op=='chapters' then
    local list=H.first(html,'div','id','chapterList')
    assert(list,'Chapter list unavailable; source layout may have changed.')
    local author=H.text(html,'span','class','text-red-500')
    local description=H.text(html,'p','style','white-space: pre-line;')
    return {title=H.text(html,'h1'),author=author,description=description,items=H.sort_chapters(H.links(list.html,home,'^https://beastnovels%.com/series/[^/]+/[%d%.]+$',true))}
  end
  local body=H.first(html,'div','class','chapter-content')
  assert(body and #body.text>0,'Chapter unavailable: locked, login required, or source layout changed.')
  return {text=body.text}
end
