--[[
    Atingle Professional Environment Wrapper
    Version: 2.0 (Optimized)
--]]

local getgenv = getgenv or function() return _G end
local env = getgenv()

-- Cache services for speed
local HttpService = game:GetService("HttpService")
local CoreGui = game:GetService("CoreGui")
local Stats = game:GetService("Stats")

--------------------------------------------------------------------------------
-- UTILITY: Closure Protection
--------------------------------------------------------------------------------
local function make_readonly(t)
    local mt = getmetatable(t) or {}
    mt.__newindex = function(_, k)
        error(string.format("Attempt to modify read-only table: %s", tostring(k)), 2)
    end
    setmetatable(t, mt)
end

-- Professional wrapper for C-closures
local function protect(fn, name)
    local protected = (newcclosure or function(f) return f end)(fn)
    -- If your C-side supports it, hook debug.getinfo here to hide the 'protected' flag
    return protected
end

--------------------------------------------------------------------------------
-- API DEFINITION
--------------------------------------------------------------------------------
local api = {}

-- Environment Access
api.getgenv = getgenv
api.getrenv = getrenv or function() return _G end
api.getreg  = getreg  or function() return debug.getregistry() end
api.getgc   = getgc   or function() return debug.getgc() end

-- UI Isolation (Hide your UI from the game)
api.gethui = function()
    if gethui then return gethui() end
    -- Fallback to a hidden folder in CoreGui
    local h = CoreGui:FindFirstChild("AtingleHidden")
    if not h then
        h = Instance.new("Folder")
        h.Name = "AtingleHidden"
        h.Parent = CoreGui
    end
    return h
end

-- Script Detection Spoofer
api.checkcaller = checkcaller or function() 
    -- If C-side hasn't implemented this, we return false to stay safe
    return false 
end

-- File & Web System
api.request = request or http_request or function(options)
    assert(type(options) == "table", "Invalid options table")
    local method = options.Method or "GET"
    local url = options.Url
    
    local success, response = pcall(function()
        if method == "GET" then
            return HttpService:GetAsync(url)
        elseif method == "POST" then
            return HttpService:PostAsync(url, options.Body or "")
        end
    end)
    
    return {
        Success = success,
        StatusCode = success and 200 or 500,
        Body = response or "Request Failed",
        Headers = {}
    }
end

-- Aliases for broader script support
api.http_request = api.request
api.HttpGet = function(self, url) return api.request({Url = url, Method = "GET"}).Body end

--------------------------------------------------------------------------------
-- DEBUG LIBRARY ENHANCEMENT
--------------------------------------------------------------------------------
local debug_overrides = {
    getupvalue    = debug.getupvalue,
    setupvalue    = debug.setupvalue,
    getconstants  = debug.getconstants or function() return {} end,
    getproto      = debug.getproto,
    getprotos     = debug.getprotos or function() return {} end,
    getinfo       = debug.getinfo,
}

env.debug = env.debug or {}
for name, func in pairs(debug_overrides) do
    local p_func = protect(func, name)
    env.debug[name] = p_func
    env[name] = p_func -- Add to global for convenience
end

--------------------------------------------------------------------------------
-- EVENT SYSTEM (Signals)
--------------------------------------------------------------------------------
local Event = {}
Event.__index = Event

function Event.new()
    return setmetatable({ _instance = Instance.new("BindableEvent") }, Event)
end

function Event:Connect(callback)
    return self._instance.Event:Connect(callback)
end

function Event:Fire(...)
    self._instance:Fire(...)
end

api.Signal = Event
api.new_signal = Event.new

--------------------------------------------------------------------------------
-- INITIALIZATION
--------------------------------------------------------------------------------
for name, func in pairs(api) do
    if not env[name] then
        env[name] = protect(func, name)
    end
end

-- Final touch: Identify the executor
env.identifyexecutor = function() return "Atingle", "v2.0" end
env.getexecutorname = env.identifyexecutor

warn("Atingle Environment Loaded Successfully")
