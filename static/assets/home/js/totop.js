var toTopButton = document.getElementById("to-top-button");

if (toTopButton) {
	window.onscroll = function() {
		if (document.body.scrollTop > 500 || document.documentElement.scrollTop > 500) {
			toTopButton.classList.add("show");
		} else {
			toTopButton.classList.remove("show");
		}
	};

	window.goToTop = function() {
		window.scrollTo({
			top: 0,
			behavior: 'smooth'
		});
	};
}