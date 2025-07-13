import { createChart, LineSeries } from 'lightweight-charts';

document.addEventListener('DOMContentLoaded', () => {
  console.log("Plot script started");

  const chartContainer = document.getElementById('chart-container');
  console.log("Chart container:", chartContainer);
  if (chartContainer) {
    console.log("Chart container clientWidth:", chartContainer.clientWidth);
  }

  const chart = createChart(chartContainer, {
    width: chartContainer.clientWidth,
    height: 300,
    layout: {
      backgroundColor: '#ffffff',
      textColor: 'rgba(33, 56, 77, 1)',
    },
    grid: {
      vertLines: {
        color: 'rgba(197, 203, 206, 0.5)',
      },
      horzLines: {
        color: 'rgba(197, 203, 206, 0.5)',
      },
    },
    timeScale: {
      timeVisible: true,
      secondsVisible: true,
    },
  });

  const lineSeries = chart.addSeries(LineSeries, {
    color: 'rgba(75, 192, 192, 1)',
    lineWidth: 2,
  });

  let lastTimestamp = 0;

  function fetchHistoryData(isInitialLoad = false) {
    const url = isInitialLoad ? '/api/current_history' : `/api/current_history?since=${lastTimestamp}`;
    
    fetch(url)
      .then(response => response.json())
      .then(apiData => {
        if (apiData.length === 0) return;

        const formattedData = apiData.map(entry => ({
          time: entry.timestamp,
          value: entry.current
        }));

        if (isInitialLoad) {
          lineSeries.setData(formattedData);
        } else {
          formattedData.forEach(dataPoint => {
            lineSeries.update(dataPoint);
          });
        }

        // Update the last timestamp from the most recent data point
        lastTimestamp = apiData[apiData.length - 1].timestamp;
      })
      .catch(err => console.error('Error fetching current history data:', err));
  }

  // Fetch full history initially, then poll for new data
  fetchHistoryData(true);
  setInterval(() => fetchHistoryData(false), 5000);
});
