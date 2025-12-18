/******/ (function(modules) { // webpackBootstrap
/******/ 	// The module cache
/******/ 	var installedModules = {};
/******/
/******/ 	// The require function
/******/ 	function __webpack_require__(moduleId) {
/******/
/******/ 		// Check if module is in cache
/******/ 		if(installedModules[moduleId])
/******/ 			return installedModules[moduleId].exports;
/******/
/******/ 		// Create a new module (and put it into the cache)
/******/ 		var module = installedModules[moduleId] = {
/******/ 			exports: {},
/******/ 			id: moduleId,
/******/ 			loaded: false
/******/ 		};
/******/
/******/ 		// Execute the module function
/******/ 		modules[moduleId].call(module.exports, module, module.exports, __webpack_require__);
/******/
/******/ 		// Flag the module as loaded
/******/ 		module.loaded = true;
/******/
/******/ 		// Return the exports of the module
/******/ 		return module.exports;
/******/ 	}
/******/
/******/
/******/ 	// expose the modules object (__webpack_modules__)
/******/ 	__webpack_require__.m = modules;
/******/
/******/ 	// expose the module cache
/******/ 	__webpack_require__.c = installedModules;
/******/
/******/ 	// __webpack_public_path__
/******/ 	__webpack_require__.p = "";
/******/
/******/ 	// Load entry module and return exports
/******/ 	return __webpack_require__(0);
/******/ })
/************************************************************************/
/******/ ([
/* 0 */
/***/ (function(module, exports, __webpack_require__) {

	__webpack_require__(1);
	module.exports = __webpack_require__(2);


/***/ }),
/* 1 */
/***/ (function(module, exports) {

	/**
	 * Copyright 2024 Google LLC
	 *
	 * Licensed under the Apache License, Version 2.0 (the "License");
	 * you may not use this file except in compliance with the License.
	 * You may obtain a copy of the License at
	 *
	 *     http://www.apache.org/licenses/LICENSE-2.0
	 *
	 * Unless required by applicable law or agreed to in writing, software
	 * distributed under the License is distributed on an "AS IS" BASIS,
	 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	 * See the License for the specific language governing permissions and
	 * limitations under the License.
	 */
	
	(function(p) {
	  if (!p === undefined) {
	    console.error('Pebble object not found!?');
	    return;
	  }
	
	  // Aliases:
	  p.on = p.addEventListener;
	  p.off = p.removeEventListener;
	
	  // For Android (WebView-based) pkjs, print stacktrace for uncaught errors:
	  if (typeof window !== 'undefined' && window.addEventListener) {
	    window.addEventListener('error', function(event) {
	      if (event.error && event.error.stack) {
	        console.error('' + event.error + '\n' + event.error.stack);
	      }
	    });
	  }
	
	})(Pebble);


/***/ }),
/* 2 */
/***/ (function(module, exports) {

	// OpenWeatherMap API Key - Get one free at https://openweathermap.org/appid
	// The API key is automatically injected from WEATHER_SECRET environment variable during build
	// REMOVED_API_KEY will be replaced with the actual key during build
	var myAPIKey = 'REMOVED_API_KEY';
	
	// Log API key status (first 8 chars only for security)
	console.log('API Key loaded: ' + (myAPIKey && myAPIKey !== 'REMOVED_API_KEY' ? myAPIKey.substring(0, 8) + '...' : 'MISSING'));
	if (!myAPIKey || myAPIKey === 'REMOVED_API_KEY') {
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


/***/ })
/******/ ]);
//# sourceMappingURL=pebble-js-app.js.map