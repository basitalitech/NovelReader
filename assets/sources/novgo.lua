local H,home=common,'https://novgo.net'
return function(op,url,query)
  if op=='browse' then
    url=url~='' and url or (query~='' and home..'/search?keyword='..H.encode(query) or home..'/latest-release-novel')
    local html=get(url)
    local items=H.links(html,home,'^https://novgo%.net/[^/]+%.html$',false)
    return {items=items,next=H.next(html,home)}
  end
  local html=get(url)
  if op=='chapters' then
    local title=H.text(html,'h1'); if title=='' then title=H.text(html,'h3','class','title') end
    local summary=H.text(html,'div','id','novel-summary-inner')
    local author=H.first(html,'meta','property','og:novel:author')
    local list=H.first(html,'div','id','list-chapter')
    assert(list,'Chapter list unavailable; source layout may have changed.')
    return {title=title,cover=H.cover(html,home),author=author and author.attrs.content or '',description=summary,items=H.links(list.html,home,'^https://novgo%.net/[^/]+/[^/]+%.html$',true),next=H.next(list.html,home)}
  end
  local body=H.first(html,'div','id','chapter-content')
  assert(body and #body.text>0,'Chapter text unavailable; source layout may have changed.')
  return {text=body.text}
end
