// Import Clay for configuration
var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// OpenWeatherMap API Key - Get one free at https://openweathermap.org/appid
// The API key is automatically injected from WEATHER_SECRET environment variable during build
// WEATHER_API_KEY_PLACEHOLDER will be replaced with the actual key during build
var myAPIKey = 'WEATHER_API_KEY_PLACEHOLDER';

// Log API key status (first 8 chars only for security)
console.log('API Key loaded: ' + (myAPIKey && myAPIKey !== 'WEATHER_API_KEY_PLACEHOLDER' ? myAPIKey.substring(0, 8) + '...' : 'MISSING'));
if (!myAPIKey || myAPIKey === 'WEATHER_API_KEY_PLACEHOLDER') {
  console.error('WARNING: No API key found! Set WEATHER_SECRET environment variable and rebuild.');
}

// BATTERY OPTIMIZATION STRATEGY:
// - GPS cached for 3 hours (major battery saver - GPS is expensive)
// - Weather fetched every 15 minutes using cached GPS
// - Weather refreshed at startup and when data is older than 15 minutes
// - Only updates when data actually changes

// GPS cache - updated every 3 hours
var cachedLocation = null;
var lastLocationTime = 0;
var GPS_CACHE_DURATION = 3 * 60 * 60 * 1000; // 3 hours in milliseconds

// Weather update tracking
var lastWeatherUpdate = 0;
var WEATHER_UPDATE_INTERVAL = 15 * 60 * 1000; // 15 minutes in milliseconds

function iconFromWeatherId(weatherId) {
  if (weatherId < 600) {
    return 2; // Rain
  } else if (weatherId < 700) {
    return 3; // Snow
  } else if (weatherId > 800) {
    return 1; // Clouds
  } else {
    return 0; // Clear
  }
}

function fetchWeather(latitude, longitude) {
  if (!myAPIKey) {
    console.error('Cannot fetch weather: API key is missing!');
    return;
  }
  
  var url = 'http://api.openweathermap.org/data/2.5/forecast?' +
    'lat=' + latitude + '&lon=' + longitude + '&cnt=1&appid=' + myAPIKey;
  console.log('Fetching forecast from: ' + url.replace(myAPIKey, 'API_KEY'));
  
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.onload = function () {
    if (req.readyState === 4) {
      if (req.status === 200) {
        console.log('Forecast API Response: ' + req.responseText);
        var response = JSON.parse(req.responseText);
        // Get the first forecast entry (next 3-hour period)
        var forecast = response.list[0];
        var temperature = Math.round(forecast.main.temp - 273.15);
        var icon = iconFromWeatherId(forecast.weather[0].id);
        var condition = forecast.weather[0].main.toLowerCase();
        console.log('Forecast Temperature: ' + temperature);
        console.log('Forecast Icon: ' + icon);
        console.log('Forecast Condition: ' + condition);
        
        // Send using numeric keys
        var dict = {
          0: icon,              // WEATHER_ICON_KEY
          1: temperature,       // WEATHER_TEMPERATURE_KEY
          2: condition          // WEATHER_CITY_KEY (now condition)
        };
        console.log('Sending to watch: ' + JSON.stringify(dict));
        Pebble.sendAppMessage(dict,
          function(e) {
            console.log('Weather sent successfully!');
            lastWeatherUpdate = Date.now();
          },
          function(e) {
            console.log('Failed to send weather: ' + JSON.stringify(e));
          }
        );
      } else {
        console.log('Weather API Error: ' + req.status);
        console.log('Response: ' + req.responseText);
      }
    }
  };
  req.onerror = function() {
    console.log('Weather API request failed');
  };
  req.send(null);
}

function locationSuccess(pos) {
  var coordinates = pos.coords;
  // Cache the location
  cachedLocation = {
    latitude: coordinates.latitude,
    longitude: coordinates.longitude
  };
  lastLocationTime = Date.now();
  console.log('GPS location cached: ' + cachedLocation.latitude + ', ' + cachedLocation.longitude);
  
  fetchWeather(coordinates.latitude, coordinates.longitude);
}

function locationError(err) {
  console.warn('location error (' + err.code + '): ' + err.message);
  
  // If we have a cached location, use it
  if (cachedLocation) {
    console.log('Using cached location due to GPS error');
    fetchWeather(cachedLocation.latitude, cachedLocation.longitude);
  } else {
    Pebble.sendAppMessage({
      2: 'error',        // WEATHER_CITY_KEY
      1: 0               // WEATHER_TEMPERATURE_KEY
    },
    function(e) {
      console.log('Error message sent to watch');
    },
    function(e) {
      console.log('Failed to send error to watch');
    });
  }
}

function getWeather(forceUpdate) {
  var now = Date.now();
  var timeSinceLastLocation = now - lastLocationTime;
  var timeSinceLastWeather = now - lastWeatherUpdate;
  
  // Force update if requested, at startup, or if weather is older than 15 minutes
  if (forceUpdate || lastWeatherUpdate === 0 || timeSinceLastWeather >= WEATHER_UPDATE_INTERVAL) {
    console.log('Weather update needed (age: ' + Math.round(timeSinceLastWeather / 60000) + ' minutes)');
    
    // If we have a cached location and it's less than 3 hours old, use it
    if (cachedLocation && timeSinceLastLocation < GPS_CACHE_DURATION) {
      console.log('Using cached GPS location (age: ' + Math.round(timeSinceLastLocation / 60000) + ' minutes)');
      fetchWeather(cachedLocation.latitude, cachedLocation.longitude);
    } else {
      // Location is stale or doesn't exist, get fresh GPS
      console.log('Requesting fresh GPS location (cache age: ' + Math.round(timeSinceLastLocation / 60000) + ' minutes)');
      window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    }
  } else {
    console.log('Weather is fresh, skipping update (age: ' + Math.round(timeSinceLastWeather / 60000) + ' minutes)');
  }
}

var locationOptions = {
  'timeout': 15000,
  'maximumAge': 60000
};

// Helper to format time ago
function getTimeAgo(timestamp) {
  if (!timestamp || timestamp === 0) {
    return 'Never';
  }
  var minutes = Math.round((Date.now() - timestamp) / 60000);
  if (minutes === 0) {
    return 'Just now';
  }
  return minutes + ' minute(s) ago';
}

Pebble.addEventListener('ready', function (e) {
  console.log('PebbleKit JS ready!');
  
  // Force fetch weather on startup to ensure fresh data
  getWeather(true);
  
  // Check for weather updates every 5 minutes, but only fetch if data is stale
  setInterval(function() {
    console.log('Checking if weather update needed...');
    getWeather();
  }, 5 * 60 * 1000); // Check every 5 minutes
});

Pebble.addEventListener('appmessage', function (e) {
  console.log('Received message from watch, requesting weather update');
  getWeather();
});

// Handle showing configuration
Pebble.addEventListener('showConfiguration', function(e) {
  console.log('Opening configuration page');
  console.log('--- Current Status ---');
  console.log('Last Weather Update: ' + getTimeAgo(lastWeatherUpdate));
  console.log('Last GPS Update: ' + getTimeAgo(lastLocationTime));
  console.log('Cached Location: ' + (cachedLocation ? cachedLocation.latitude.toFixed(4) + ', ' + cachedLocation.longitude.toFixed(4) : 'None'));
  console.log('API Key Status: ' + ((myAPIKey && myAPIKey !== 'WEATHER_API_KEY_PLACEHOLDER') ? 'Configured' : 'Missing'));
  console.log('---------------------');
  Pebble.openURL(clay.generateUrl());
});

// Handle configuration close
Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) {
    console.log('Configuration cancelled');
    return;
  }

  // Get the Clay response
  var dict = clay.getSettings(e.response);
  console.log('Config response:', JSON.stringify(dict));
  
  // Check if refresh buttons were clicked
  if (dict && dict['refresh-weather']) {
    console.log('Refresh weather button clicked');
    getWeather(true);
  }
  
  if (dict && dict['refresh-gps']) {
    console.log('Refresh GPS button clicked');
    // Force GPS refresh by invalidating cache
    lastLocationTime = 0;
    cachedLocation = null;
    getWeather(true);
  }
});
