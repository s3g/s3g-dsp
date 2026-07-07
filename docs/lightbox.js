(() => {
  const triggers = Array.from(document.querySelectorAll("[data-lightbox-image]"));
  const lightbox = document.querySelector("[data-page-lightbox]");
  if (!triggers.length || !lightbox) return;
  const image = document.querySelector("[data-page-lightbox-image]");
  const close = document.querySelector("[data-page-lightbox-close]");

  function show(trigger) {
    const img = trigger.querySelector("img");
    image.src = trigger.dataset.lightboxImage;
    image.alt = img ? img.alt : "";
    lightbox.setAttribute("aria-hidden", "false");
    document.body.classList.add("media-lightbox-open");
  }

  function hide() {
    lightbox.setAttribute("aria-hidden", "true");
    document.body.classList.remove("media-lightbox-open");
    image.src = "";
  }

  triggers.forEach(trigger => trigger.addEventListener("click", () => show(trigger)));
  close.addEventListener("click", hide);
  lightbox.addEventListener("click", event => {
    if (event.target === lightbox) hide();
  });
  document.addEventListener("keydown", event => {
    if (lightbox.getAttribute("aria-hidden") === "true") return;
    if (event.key === "Escape") hide();
  });
})();
