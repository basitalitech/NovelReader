-- Small strict JSON decoder for public source responses. No code evaluation.
local function decode(s)
  local at, depth = 1, 0
  local function ws() local _, e = s:find('^[ \t\r\n]*', at); at = (e or at - 1) + 1 end
  local value
  local function str()
    assert(s:sub(at, at) == '"', 'Expected JSON string'); at = at + 1
    local out = {}
    while at <= #s do
      local c = s:sub(at, at); at = at + 1
      if c == '"' then return table.concat(out) end
      if c == '\\' then
        c = s:sub(at, at); at = at + 1
        local escapes = {['"']='"', ['\\']='\\', ['/']='/', b='\b', f='\f', n='\n', r='\r', t='\t'}
        if c == 'u' then
          local hex = s:sub(at, at + 3); assert(hex:match('^%x%x%x%x$'), 'Invalid JSON escape')
          local n = tonumber(hex, 16); at = at + 4
          if n >= 0xd800 and n <= 0xdbff then
            assert(s:sub(at, at+1) == '\\u', 'Invalid surrogate'); at = at + 2
            local low = tonumber(s:sub(at, at+3),16); at = at + 4
            assert(low and low >= 0xdc00 and low <= 0xdfff, 'Invalid surrogate')
            n = 0x10000 + (n-0xd800)*1024 + low-0xdc00
          end
          assert(n < 0xd800 or n > 0xdfff, 'Invalid surrogate')
          out[#out+1] = utf8.char(n)
        else assert(escapes[c], 'Invalid JSON escape'); out[#out+1] = escapes[c] end
      else assert(c:byte() >= 32, 'Invalid JSON string'); out[#out+1] = c end
    end
    error('Unterminated JSON string')
  end
  value = function()
    ws(); depth = depth + 1; assert(depth <= 64, 'JSON too deep')
    local c, result = s:sub(at, at)
    if c == '"' then result = str()
    elseif c == '{' or c == '[' then
      local object, close = c == '{', c == '{' and '}' or ']'
      at = at + 1; result = {}; ws()
      if s:sub(at, at) ~= close then
        while true do
          local key
          if object then ws(); key = str(); ws(); assert(s:sub(at,at)==':', 'Expected colon'); at=at+1 end
          local v = value(); result[object and key or #result+1] = v
          ws(); if s:sub(at,at) == close then break end
          assert(s:sub(at,at) == ',', 'Expected comma'); at=at+1
        end
      end
      at=at+1
    elseif s:sub(at,at+3)=='true' then result=true; at=at+4
    elseif s:sub(at,at+4)=='false' then result=false; at=at+5
    elseif s:sub(at,at+3)=='null' then result=false; at=at+4
    else
      local token=s:match('^%-?%d+%.?%d*[eE]?[+-]?%d*',at)
      result=token and tonumber(token); assert(result,'Invalid JSON value'); at=at+#token
    end
    depth=depth-1; return result
  end
  local result=value(); ws(); assert(at>#s,'Trailing JSON'); return result
end
return decode
