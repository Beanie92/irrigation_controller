import { Chart, registerables } from 'chart.js';
import 'chartjs-adapter-date-fns';
Chart.register(...registerables);

document.addEventListener('DOMContentLoaded', () => {
  const ctx = document.getElementById('currentChart').getContext('2d');
  const currentChart = new Chart(ctx, {
    type: 'line',
    data: {
      datasets: [{
        label: 'Current [A]',
        data: [],
        borderColor: 'rgba(75, 192, 192, 1)',
        tension: 0.1
      }]
    },
    options: {
      maintainAspectRatio: false,
      scales: {
        x: {
          type: 'time',
          time: {
            unit: 'second'
          }
        },
        y: {
          beginAtZero: false,
        }
      }
    }
  });

  let lastTimestamp = 0;

  function fetchHistoryData(isInitialLoad = false) {
    const url = isInitialLoad ? '/api/current_history' : `/api/current_history?since=${lastTimestamp}`;
    
    fetch(url)
      .then(response => response.json())
      .then(apiData => {
        if (apiData.length === 0) return;

        const newData = apiData.map(entry => ({
          x: entry.timestamp,
          y: entry.current
        }));

        if (isInitialLoad) {
          currentChart.data.datasets[0].data = newData;
        } else {
          currentChart.data.datasets[0].data.push(...newData);
        }
        
        currentChart.update();

        // Update the last timestamp from the most recent data point
        lastTimestamp = apiData[apiData.length - 1].timestamp;
      })
      .catch(err => console.error('Error fetching current history data:', err));
  }

  const autoRefreshCheckbox = document.getElementById('autoRefreshCheckbox');
  let refreshInterval;

  function startAutoRefresh() {
    if (!refreshInterval) {
      refreshInterval = setInterval(() => fetchHistoryData(false), 5000);
    }
  }

  function stopAutoRefresh() {
    if (refreshInterval) {
      clearInterval(refreshInterval);
      refreshInterval = null;
    }
  }

  autoRefreshCheckbox.addEventListener('change', () => {
    if (autoRefreshCheckbox.checked) {
      startAutoRefresh();
    } else {
      stopAutoRefresh();
    }
  });

  // Fetch full history initially and start polling
  fetchHistoryData(true);
  startAutoRefresh();
});
