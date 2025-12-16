// OpenWeatherMap API Key - Get one free at https://openweathermap.org/appid
var myAPIKey = 'REMOVED_SECRET';

// BATTERY OPTIMIZATION STRATEGY:
// - GPS cached for 3 hours (major battery saver - GPS is expensive)
// - Weather fetched every 15 minutes using cached GPS
// - No weather fetch on startup (uses persistent cache from C code)
// - Only updates when data actually changes

// GPS cache - updated every 3 hours
var cachedLocation = null;
var lastLocationTime = 0;
var GPS_CACHE_DURATION = 3 * 60 * 60 * 1000; // 3 hours in milliseconds

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
  var req = new XMLHttpRequest();
  req.open('GET', 'http://api.openweathermap.org/data/2.5/weather?' +
    'lat=' + latitude + '&lon=' + longitude + '&cnt=1&appid=' + myAPIKey, true);
  req.onload = function () {
    if (req.readyState === 4) {
      if (req.status === 200) {
        console.log('Weather API Response: ' + req.responseText);
        var response = JSON.parse(req.responseText);
        var temperature = Math.round(response.main.temp - 273.15);
        var icon = iconFromWeatherId(response.weather[0].id);
        var condition = response.weather[0].main.toLowerCase();
        console.log('Temperature: ' + temperature);
        console.log('Icon: ' + icon);
        console.log('Condition: ' + condition);
        
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

function getWeather() {
  var now = Date.now();
  var timeSinceLastLocation = now - lastLocationTime;
  
  // If we have a cached location and it's less than 3 hours old, use it
  if (cachedLocation && timeSinceLastLocation < GPS_CACHE_DURATION) {
    console.log('Using cached GPS location (age: ' + Math.round(timeSinceLastLocation / 60000) + ' minutes)');
    fetchWeather(cachedLocation.latitude, cachedLocation.longitude);
  } else {
    // Location is stale or doesn't exist, get fresh GPS
    console.log('Requesting fresh GPS location (cache age: ' + Math.round(timeSinceLastLocation / 60000) + ' minutes)');
    window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
  }
}

var locationOptions = {
  'timeout': 15000,
  'maximumAge': 60000
};

Pebble.addEventListener('ready', function (e) {
  console.log('PebbleKit JS ready!');
  
  // Fetch weather once on startup to ensure data is available
  // The C code will display cached data first, then update with fresh data
  getWeather();
  
  // Update weather every 15 minutes using cached GPS (only refresh GPS every 3 hours)
  setInterval(function() {
    console.log('Auto-updating weather...');
    getWeather();
  }, 15 * 60 * 1000); // 15 minutes in milliseconds
});

Pebble.addEventListener('appmessage', function (e) {
  console.log('Received message from watch, requesting weather update');
  getWeather();
});
