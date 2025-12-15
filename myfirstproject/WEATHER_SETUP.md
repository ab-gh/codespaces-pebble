# Weather Setup Instructions

## Getting an OpenWeatherMap API Key

1. **Go to OpenWeatherMap**
   - Visit: https://openweathermap.org/appid

2. **Create a Free Account**
   - Click "Sign Up" in the top right
   - Fill in your details (name, email, password)
   - Verify your email address

3. **Get Your API Key**
   - After logging in, click on your username in the top right
   - Select "My API keys" from the dropdown
   - You'll see a default API key already created
   - Copy this key (it looks like: `abc123def456...`)

4. **Add Key to Your App**
   - Open `src/pkjs/index.js`
   - Find the line: `var myAPIKey = '';`
   - Paste your API key between the quotes: `var myAPIKey = 'abc123def456...';`
   - Save the file

5. **Rebuild Your App**
   ```bash
   pebble build
   pebble install --emulator basalt
   ```

## How It Works

- Weather updates when the watch face loads
- The app sends your location to OpenWeatherMap
- Temperature is displayed in Celsius on the right side
- Weather updates can be requested by sending a message from the watch to JS

## Troubleshooting

- **API Key not working?** It can take a few minutes after signup for the key to activate
- **No weather showing?** Check the JavaScript console in the emulator for error messages
- **Location errors?** The emulator should provide a fake location, but check Settings > Location

## Free Tier Limits

- 1,000 API calls per day
- 60 calls per minute
- Perfect for a watch face that updates periodically
