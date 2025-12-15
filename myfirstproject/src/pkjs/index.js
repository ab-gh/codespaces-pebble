// OpenWeatherMap API Key - Get one free at https://openweathermap.org/appid
var myAPIKey = 'REMOVED_API_KEY';

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
  fetchWeather(coordinates.latitude, coordinates.longitude);
}

function locationError(err) {
  console.warn('location error (' + err.code + '): ' + err.message);
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

var locationOptions = {
  'timeout': 15000,
  'maximumAge': 60000
};

Pebble.addEventListener('ready', function (e) {
  console.log('PebbleKit JS ready!');
  
  // Send fake weather data for testing (comment this out once API key works)
  console.log('Sending fake weather data for testing...');
  Pebble.sendAppMessage({
    0: 0,              // WEATHER_ICON_KEY
    1: 19,             // WEATHER_TEMPERATURE_KEY (19c)
    2: 'clear'         // WEATHER_CITY_KEY (condition)
  },
  function(e) {
    console.log('Fake weather sent successfully!');
  },
  function(e) {
    console.log('Failed to send fake weather: ' + JSON.stringify(e));
  });
  
  // Comment out the real API call until your key activates (10-15 min after creation)
  // window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError,
  //   locationOptions);
});

Pebble.addEventListener('appmessage', function (e) {
  console.log('Received message from watch, requesting weather update');
  window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError,
    locationOptions);
});
